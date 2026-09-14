#define NOMINMAX

#include "AppSettings.h"
#include "AppState.h"
#include "PathUtils.h"

#include <windows.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

namespace {

std::wstring toWide(const std::string& text) {
    if (text.empty()) return L"";
    const int length = MultiByteToWideChar(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return L"";
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        &result[0], length);
    return result;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return "";
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        &result[0], length, nullptr, nullptr);
    return result;
}

std::wstring localAppDataDirectory() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required == 0) return L"";
    std::vector<wchar_t> buffer(required);
    const DWORD written = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size()) return L"";
    return std::wstring(buffer.data(), written);
}

std::wstring normalizedPath(const std::string& path) {
    if (path.empty()) return L"";
    std::error_code error;
    std::filesystem::path value = std::filesystem::weakly_canonical(
        std::filesystem::u8path(path), error);
    if (error) {
        error.clear();
        value = std::filesystem::absolute(std::filesystem::u8path(path), error);
    }
    std::wstring result = error ? toWide(path) : value.wstring();
    std::replace(result.begin(), result.end(), L'/', L'\\');
    while (result.size() > 3 && result.back() == L'\\') result.pop_back();
    return result;
}

bool samePath(const std::string& left, const std::string& right) {
    const std::wstring normalizedLeft = normalizedPath(left);
    const std::wstring normalizedRight = normalizedPath(right);
    return !normalizedLeft.empty() && !normalizedRight.empty() &&
        _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
}

std::string portableModelPath(const std::string& path) {
    const std::wstring full = normalizedPath(path);
    const std::wstring assets = normalizedPath(assetRoot());
    if (!full.empty() && !assets.empty() && full.size() > assets.size() &&
        _wcsnicmp(full.c_str(), assets.c_str(), assets.size()) == 0 &&
        full[assets.size()] == L'\\') {
        return "assets" + toUtf8(full.substr(assets.size()));
    }
    return path;
}

std::string singleLine(std::string value) {
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::replace(value.begin(), value.end(), '\n', ' ');
    return value;
}

bool parseBool(const std::map<std::string, std::string>& values,
               const char* key, bool& output) {
    const auto found = values.find(key);
    if (found == values.end()) return false;
    if (found->second == "1" || found->second == "true") {
        output = true;
        return true;
    }
    if (found->second == "0" || found->second == "false") {
        output = false;
        return true;
    }
    return false;
}

bool parseFloat(const std::map<std::string, std::string>& values,
                const char* key, float minimum, float maximum, float& output) {
    const auto found = values.find(key);
    if (found == values.end()) return false;
    errno = 0;
    char* end = nullptr;
    const float value = std::strtof(found->second.c_str(), &end);
    if (errno == ERANGE || end == found->second.c_str() || !end || *end != '\0' ||
        !std::isfinite(value)) {
        return false;
    }
    output = std::clamp(value, minimum, maximum);
    return true;
}

} // namespace

std::string appSettingsPath() {
    std::wstring base = localAppDataDirectory();
    if (!base.empty()) {
        std::filesystem::path path(base);
        path /= L"Live2DandSpineDesktopPets";
        path /= L"settings.ini";
        return path.u8string();
    }
    return executableDirectory() + "\\settings.ini";
}

bool loadAppSettings(AppState& state) {
    std::ifstream input(std::filesystem::u8path(appSettingsPath()), std::ios::binary);
    if (!input) return false;

    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }

    parseFloat(values, "positionX", -1000000.0f, 1000000.0f, state.positionX);
    parseFloat(values, "positionY", -1000000.0f, 1000000.0f, state.positionY);
    parseFloat(values, "scale", 0.05f, 5.0f, state.scale);
    parseFloat(values, "animationSpeed", 0.0f, 5.0f, state.animationSpeed);
    parseFloat(values, "animationMix", 0.0f, 5.0f, state.animationMix);
    parseBool(values, "animationLoop", state.animationLoop);
    parseBool(values, "alwaysOnTop", state.alwaysOnTop);
    parseBool(values, "mouseDragEnabled", state.mouseDragEnabled);
    state.alwaysOnTopChanged = true;

    const auto animation = values.find("animation");
    if (animation != values.end()) state.currentAnimation = animation->second;
    state.overlayAnimations.clear();
    for (int index = 0; index < 32; ++index) {
        const auto overlay = values.find("overlayAnimation" + std::to_string(index));
        if (overlay == values.end()) break;
        if (!overlay->second.empty()) state.overlayAnimations.push_back(overlay->second);
    }
    const auto skin = values.find("skinOrExpression");
    if (skin != values.end()) state.currentSkin = skin->second;

    const auto model = values.find("modelPath");
    if (model != values.end() && !model->second.empty()) {
        const std::string savedPath = resolveAssetPath(model->second);
        for (int index = 0; index < static_cast<int>(state.models.size()); ++index) {
            if (samePath(savedPath, state.models[index].skeletonPath)) {
                state.currentModelIndex = index;
                break;
            }
        }
    }
    return true;
}

bool saveAppSettings(const AppState& state) {
    const std::filesystem::path path = std::filesystem::u8path(appSettingsPath());
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;

    output << "# Live2D and Spine Desktop Pets settings\n";
    output << "version=1\n";
    if (state.currentModelIndex >= 0 &&
        state.currentModelIndex < static_cast<int>(state.models.size())) {
        const ModelConfig& model = state.models[state.currentModelIndex];
        output << "modelPath=" << singleLine(portableModelPath(model.skeletonPath)) << '\n';
        output << "modelType="
               << (model.type == PetModelType::Live2D ? "live2d" : "spine") << '\n';
    } else {
        output << "modelPath=\n";
    }
    output << "animation=" << singleLine(state.currentAnimation) << '\n';
    for (size_t index = 0; index < state.overlayAnimations.size(); ++index) {
        output << "overlayAnimation" << index << '='
               << singleLine(state.overlayAnimations[index]) << '\n';
    }
    output << "skinOrExpression=" << singleLine(state.currentSkin) << '\n';
    output << std::setprecision(9);
    output << "positionX=" << state.positionX << '\n';
    output << "positionY=" << state.positionY << '\n';
    output << "scale=" << state.scale << '\n';
    output << "animationSpeed=" << state.animationSpeed << '\n';
    output << "animationMix=" << state.animationMix << '\n';
    output << "animationLoop=" << (state.animationLoop ? 1 : 0) << '\n';
    output << "alwaysOnTop=" << (state.alwaysOnTop ? 1 : 0) << '\n';
    output << "mouseDragEnabled=" << (state.mouseDragEnabled ? 1 : 0) << '\n';
    return output.good();
}
