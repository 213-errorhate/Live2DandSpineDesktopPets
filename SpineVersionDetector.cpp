#include "SpineVersionDetector.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <regex>
#include <vector>

namespace {

std::wstring toWidePath(const char* path) {
    if (!path) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, result.data(), size);
    result.pop_back();
    return result;
}

bool readFileBytes(const char* path, std::vector<std::uint8_t>& bytes) {
    const std::wstring widePath = toWidePath(path);
    FILE* file = nullptr;
    if (!widePath.empty()) _wfopen_s(&file, widePath.c_str(), L"rb");
    if (!file) return false;
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const auto length = _ftelli64(file);
    if (length <= 0 || length > 64 * 1024 * 1024 || _fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(length));
    const bool ok = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    return ok;
}

bool parseVersionText(const std::string& text, SpineDataVersion& result) {
    static const std::regex versionPattern(R"(^\s*([0-9]+)\.([0-9]+)(?:\.[0-9A-Za-z._-]+)*\s*$)");
    std::smatch match;
    if (!std::regex_match(text, match, versionPattern)) return false;
    result.major = std::stoi(match[1].str());
    result.minor = std::stoi(match[2].str());
    result.full = text;
    while (!result.full.empty() && std::isspace(static_cast<unsigned char>(result.full.front())))
        result.full.erase(result.full.begin());
    while (!result.full.empty() && std::isspace(static_cast<unsigned char>(result.full.back())))
        result.full.pop_back();
    return true;
}

bool readVarint(const std::vector<std::uint8_t>& bytes, size_t& offset, std::uint32_t& value) {
    value = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (offset >= bytes.size()) return false;
        const std::uint8_t byte = bytes[offset++];
        value |= static_cast<std::uint32_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) return true;
    }
    return false;
}

bool readBinaryString(const std::vector<std::uint8_t>& bytes, size_t& offset, std::string& value) {
    std::uint32_t encodedLength = 0;
    if (!readVarint(bytes, offset, encodedLength) || encodedLength == 0) {
        value.clear();
        return encodedLength == 0;
    }
    const size_t length = static_cast<size_t>(encodedLength - 1);
    if (length > 1024 || offset + length > bytes.size()) return false;
    value.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
    offset += length;
    return true;
}

bool parseLegacyBinary(const std::vector<std::uint8_t>& bytes, SpineDataVersion& result) {
    size_t offset = 0;
    std::string hash;
    std::string version;
    return readBinaryString(bytes, offset, hash) && readBinaryString(bytes, offset, version) &&
           parseVersionText(version, result);
}

bool parseModernBinary(const std::vector<std::uint8_t>& bytes, SpineDataVersion& result) {
    if (bytes.size() < 9) return false;
    size_t offset = 8; // Spine 4.x stores a fixed 64-bit export hash first.
    std::string version;
    return readBinaryString(bytes, offset, version) && parseVersionText(version, result);
}

} // namespace

std::string SpineDataVersion::family() const {
    return valid() ? std::to_string(major) + "." + std::to_string(minor) : std::string();
}

SpineDataVersion detectSpineDataVersion(const char* skeletonPath) {
    SpineDataVersion result;
    if (!skeletonPath || !*skeletonPath) {
        result.error = "Skeleton path is empty.";
        return result;
    }

    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(skeletonPath, bytes)) {
        result.error = std::string("Unable to read skeleton file: ") + skeletonPath;
        return result;
    }

    std::string lowerPath(skeletonPath);
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    const bool json = lowerPath.size() >= 5 && lowerPath.compare(lowerPath.size() - 5, 5, ".json") == 0;
    if (json) {
        const std::string document(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        static const std::regex spineField(R"spine("spine"\s*:\s*"([^"]+)")spine");
        std::smatch match;
        if (std::regex_search(document, match, spineField) && parseVersionText(match[1].str(), result))
            return result;
        result.error = "The Spine JSON file does not contain a valid skeleton.spine version.";
        return result;
    }

    if (parseLegacyBinary(bytes, result) || parseModernBinary(bytes, result)) return result;
    result.error = "Unable to detect the Spine binary version safely.";
    return result;
}
