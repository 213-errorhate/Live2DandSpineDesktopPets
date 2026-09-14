#define _CRT_SECURE_NO_WARNINGS

#include "ModelRegistry.h"
#include "AppState.h"
#include "PathUtils.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::wstring toWidePath(const std::string& path) {
    if (path.empty()) return L"";
    int length = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (length <= 0) return L"";
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &result[0], length);
    if (!result.empty()) result.pop_back();
    return result;
}

bool copyFileUtf8(const std::string& from, const std::string& to) {
    return CopyFileW(toWidePath(from).c_str(), toWidePath(to).c_str(), FALSE) != FALSE;
}

bool fileExists(const std::string& path) {
    return GetFileAttributesW(toWidePath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string dirName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool ensureDirectory(const std::string& path) {
    if (path.empty()) return false;
    DWORD attributes = GetFileAttributesW(toWidePath(path).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const std::string parent = dirName(path);
    if (parent.empty() || parent == path) return false;
    if (!ensureDirectory(parent)) return false;
    return CreateDirectoryW(toWidePath(path).c_str(), nullptr) != FALSE ||
        GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring fullPath(const std::string& path) {
    std::wstring wide = toWidePath(path);
    DWORD size = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
    if (!size) return L"";
    std::wstring result(size, L'\0');
    DWORD written = GetFullPathNameW(wide.c_str(), size, &result[0], nullptr);
    if (!written || written >= size) return L"";
    result.resize(written);
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/'))
        result.pop_back();
    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}

std::wstring wideDirName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

bool samePath(const std::wstring& left, const std::wstring& right) {
    return !left.empty() && !right.empty() && _wcsicmp(left.c_str(), right.c_str()) == 0;
}

void removeEmptyParents(const std::string& filePath, const std::wstring& stopDirectory) {
    std::wstring current = wideDirName(fullPath(filePath));
    while (!current.empty() && !samePath(current, stopDirectory) &&
        current.size() > stopDirectory.size() &&
        _wcsnicmp(current.c_str(), stopDirectory.c_str(), stopDirectory.size()) == 0 &&
        current[stopDirectory.size()] == L'\\') {
        if (!RemoveDirectoryW(current.c_str())) break;
        current = wideDirName(current);
    }
}

std::string registryPath(const std::string& path) {
    const std::wstring full = fullPath(path);
    const std::wstring assets = fullPath(assetRoot());
    if (!full.empty() && !assets.empty() && full.size() > assets.size() &&
        _wcsnicmp(full.c_str(), assets.c_str(), assets.size()) == 0 &&
        full[assets.size()] == L'\\') {
        std::wstring relative = L"assets" + full.substr(assets.size());
        int bytes = WideCharToMultiByte(CP_UTF8, 0, relative.c_str(),
            static_cast<int>(relative.size()), nullptr, 0, nullptr, nullptr);
        std::string result(bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, relative.c_str(),
            static_cast<int>(relative.size()), &result[0], bytes, nullptr, nullptr);
        return result;
    }
    return path;
}

std::vector<std::string> atlasImageNames(const std::string& atlasPath) {
    std::vector<std::string> names;

    FILE* file = _wfopen(toWidePath(atlasPath).c_str(), L"rb");
    if (!file) return names;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::vector<char> data(length > 0 ? length : 0);
    if (length > 0) fread(data.data(), 1, length, file);
    fclose(file);

    std::string text(data.begin(), data.end());
    std::istringstream stream(text);
    std::string line;
    bool expectPageName = true;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            expectPageName = true;
            continue;
        }
        if (expectPageName) {
            names.push_back(line);
            expectPageName = false;
        }
    }
    return names;
}

bool modelFilesExist(const ModelConfig& config) {
    if (config.type == PetModelType::Live2D)
        return fileExists(config.skeletonPath);
    if (!fileExists(config.atlasPath) || !fileExists(config.skeletonPath))
        return false;

    std::string sourceDir = dirName(config.atlasPath);
    for (const std::string& image : atlasImageNames(config.atlasPath)) {
        std::string imagePath = sourceDir.empty() ? image : sourceDir + "\\" + image;
        if (!fileExists(imagePath)) return false;
    }
    return true;
}

} // namespace

bool loadModelRegistry(const std::string& path, std::vector<ModelConfig>& models) {
    std::ifstream in(path);
    if (!in) return false;

    models.clear();
    std::vector<ModelConfig> validModels;
    int totalCount = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields;
        std::istringstream fieldsStream(line);
        std::string field;
        while (std::getline(fieldsStream, field, '\t')) fields.push_back(field);
        if (fields.size() < 3) continue;
        ModelConfig config;
        config.name = fields[0];
        config.atlasPath = fields[1] == "-" ? "" : resolveAssetPath(fields[1]);
        config.skeletonPath = resolveAssetPath(fields[2]);
        if (fields.size() >= 4) config.premultipliedAlpha = fields[3] != "straight";
        if (fields.size() >= 5) config.managedFiles = fields[4] == "managed";
        if (fields.size() >= 6 && fields[5] == "live2d") config.type = PetModelType::Live2D;
        ++totalCount;
        if (modelFilesExist(config))
            validModels.push_back(std::move(config));
    }
    in.close();

    bool cleaned = validModels.size() < static_cast<size_t>(totalCount);
    models = std::move(validModels);
    if (cleaned) saveModelRegistry(path, models);
    return true;
}

bool saveModelRegistry(const std::string& path, const std::vector<ModelConfig>& models) {
    if (!ensureDirectory(dirName(path))) return false;
    std::ofstream out(path);
    if (!out) return false;

    out << "# Pet model registry\n";
    for (const auto& config : models)
        out << config.name << '\t' << (config.atlasPath.empty() ? "-" : registryPath(config.atlasPath)) << '\t'
            << registryPath(config.skeletonPath) << '\t'
            << (config.premultipliedAlpha ? "pma" : "straight") << '\t'
            << (config.managedFiles ? "managed" : "external") << '\t'
            << (config.type == PetModelType::Live2D ? "live2d" : "spine") << '\n';
    return true;
}

bool discoverLive2DModels(const std::string& root, std::vector<ModelConfig>& models) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (!fs::is_directory(fs::u8path(root), error)) return false;

    bool added = false;
    fs::recursive_directory_iterator iterator(
        fs::u8path(root), fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (!iterator->is_regular_file(error)) continue;

        const std::string filename = iterator->path().filename().u8string();
        std::string lower = filename;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        constexpr const char* suffix = ".model3.json";
        if (lower.size() < 12 || lower.compare(lower.size() - 12, 12, suffix) != 0)
            continue;

        const std::string path = iterator->path().u8string();
        const std::wstring candidatePath = fullPath(path);
        const bool duplicate = std::any_of(models.begin(), models.end(),
            [&candidatePath](const ModelConfig& model) {
                return samePath(fullPath(model.skeletonPath), candidatePath);
            });
        if (duplicate) continue;

        ModelConfig config;
        config.name = filename.substr(0, filename.size() - 12);
        config.skeletonPath = path;
        config.premultipliedAlpha = false;
        config.managedFiles = false;
        config.type = PetModelType::Live2D;
        models.push_back(std::move(config));
        added = true;
    }
    return added;
}

bool importModelFiles(const ModelConfig& source, const std::string& assetsRoot, ModelConfig& dest) {
    if (source.type != PetModelType::Spine) return false;
    std::string destDir = assetsRoot + "\\" + source.name;
    if (!ensureDirectory(assetsRoot) || fileExists(destDir) || !ensureDirectory(destDir))
        return false;

    dest.name = source.name;
    dest.atlasPath = destDir + "\\" + fileName(source.atlasPath);
    dest.skeletonPath = destDir + "\\" + fileName(source.skeletonPath);
    dest.premultipliedAlpha = source.premultipliedAlpha;
    dest.managedFiles = true;
    dest.type = PetModelType::Spine;

    if (!copyFileUtf8(source.atlasPath, dest.atlasPath) ||
        !copyFileUtf8(source.skeletonPath, dest.skeletonPath)) {
        removeModelFiles(dest);
        return false;
    }

    std::string sourceDir = dirName(source.atlasPath);
    for (const std::string& image : atlasImageNames(source.atlasPath)) {
        std::string from = sourceDir.empty() ? image : sourceDir + "\\" + image;
        std::string to = destDir + "\\" + image;
        if (!isPathInsideDirectory(to, destDir) ||
            !ensureDirectory(dirName(to)) || !copyFileUtf8(from, to)) {
            removeModelFiles(dest);
            return false;
        }
    }
    return true;
}

bool removeModelFiles(const ModelConfig& config) {
    if (!config.managedFiles) return true;
    std::string dir = dirName(config.atlasPath);
    if (dir.empty()) return false;

    const std::wstring modelDir = fullPath(dir);
    const std::wstring spineRoot = fullPath(assetRoot() + "\\spine");
    if (!samePath(wideDirName(modelDir), spineRoot)) return false;

    const std::vector<std::string> images = atlasImageNames(config.atlasPath);
    bool success = true;
    for (const std::string& image : images) {
        const std::string path = dir + "\\" + image;
        if (!isPathInsideDirectory(path, dir)) {
            success = false;
            continue;
        }
        if (fileExists(path) && !DeleteFileW(toWidePath(path).c_str())) {
            success = false;
        } else {
            removeEmptyParents(path, modelDir);
        }
    }
    if (!isPathInsideDirectory(config.skeletonPath, dir) ||
        !isPathInsideDirectory(config.atlasPath, dir)) {
        return false;
    }
    if (fileExists(config.skeletonPath) &&
        !DeleteFileW(toWidePath(config.skeletonPath).c_str())) success = false;
    if (fileExists(config.atlasPath) &&
        !DeleteFileW(toWidePath(config.atlasPath).c_str())) success = false;
    RemoveDirectoryW(modelDir.c_str());
    return success;
}

bool isPathInsideDirectory(const std::string& path, const std::string& directory) {
    const std::wstring item = fullPath(path);
    const std::wstring root = fullPath(directory);
    return !item.empty() && !root.empty() && item.size() > root.size() &&
        _wcsnicmp(item.c_str(), root.c_str(), root.size()) == 0 &&
        item[root.size()] == L'\\';
}
