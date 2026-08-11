#define _CRT_SECURE_NO_WARNINGS

#include "ModelRegistry.h"
#include "AppState.h"
#include "PathUtils.h"

#include <windows.h>
#include <cstdio>
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
    std::string previous;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("size:", 0) == 0 && !previous.empty())
            names.push_back(previous);
        previous = line;
    }
    return names;
}

bool modelFilesExist(const ModelConfig& config) {
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
        size_t p1 = line.find('\t');
        if (p1 == std::string::npos) continue;
        size_t p2 = line.find('\t', p1 + 1);
        if (p2 == std::string::npos) continue;

        ModelConfig config;
        config.name = line.substr(0, p1);
        config.atlasPath = resolveAssetPath(line.substr(p1 + 1, p2 - p1 - 1));
        config.skeletonPath = resolveAssetPath(line.substr(p2 + 1));
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
    std::ofstream out(path);
    if (!out) return false;

    out << "# Pet model registry\n";
    for (const auto& config : models)
        out << config.name << '\t' << config.atlasPath << '\t' << config.skeletonPath << '\n';
    return true;
}

bool importModelFiles(const ModelConfig& source, const std::string& assetsRoot, ModelConfig& dest) {
    std::string destDir = assetsRoot + "\\" + source.name;
    CreateDirectoryW(toWidePath(destDir).c_str(), nullptr);

    dest.name = source.name;
    dest.atlasPath = destDir + "\\" + fileName(source.atlasPath);
    dest.skeletonPath = destDir + "\\" + fileName(source.skeletonPath);

    if (!copyFileUtf8(source.atlasPath, dest.atlasPath)) return false;
    if (!copyFileUtf8(source.skeletonPath, dest.skeletonPath)) return false;

    std::string sourceDir = dirName(source.atlasPath);
    for (const std::string& image : atlasImageNames(source.atlasPath)) {
        std::string from = sourceDir.empty() ? image : sourceDir + "\\" + image;
        std::string to = destDir + "\\" + image;
        copyFileUtf8(from, to);
    }
    return true;
}

bool removeModelFiles(const ModelConfig& config) {
    std::string dir = dirName(config.atlasPath);
    if (dir.empty()) return false;

    std::string normalized = dir;
    for (char& ch : normalized)
        if (ch == '/') ch = '\\';
    std::string assetsSpine = assetRoot() + "\\spine";
    if (normalized.find(assetsSpine) != 0) return false;
    if (normalized.find("..") != std::string::npos) return false;

    std::wstring pattern = toWidePath(dir) + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) return false;

    do {
        std::wstring name = findData.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = toWidePath(dir) + L"\\" + name;
        DeleteFileW(full.c_str());
    } while (FindNextFileW(find, &findData));
    FindClose(find);

    return RemoveDirectoryW(toWidePath(dir).c_str()) != FALSE;
}
