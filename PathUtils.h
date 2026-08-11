#pragma once

#include <string>
#include <windows.h>

inline std::string executableDirectory() {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return "";

    std::wstring wide(path, length);
    size_t slash = wide.find_last_of(L"\\/");
    if (slash != std::wstring::npos) wide = wide.substr(0, slash);

    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return "";
    std::string result(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                        &result[0], bytes, nullptr, nullptr);
    return result;
}

inline std::wstring toWideUtf8(const std::string& text) {
    if (text.empty()) return L"";
    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                     nullptr, 0);
    if (length <= 0) return L"";
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(),
                        &result[0], length);
    return result;
}

inline std::string assetRoot() {
    std::string dir = executableDirectory();
    while (!dir.empty()) {
        std::wstring candidate = toWideUtf8(dir) + L"\\assets";
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return dir + "\\assets";
        size_t slash = dir.find_last_of("\\/");
        if (slash == std::string::npos) break;
        dir = dir.substr(0, slash);
    }
    return executableDirectory() + "\\assets";
}

inline std::string resolveAssetPath(const std::string& path) {
    if (path.empty()) return path;
    if (path.size() >= 2 && path[1] == ':') return path;       // drive path
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') return path; // UNC
    if (!path.empty() && path[0] == '/') return path;
    if (path.rfind("assets\\", 0) == 0 || path.rfind("assets/", 0) == 0)
        return assetRoot() + "\\" + path.substr(7);
    return executableDirectory() + "\\" + path;
}
