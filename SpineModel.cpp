#define NOMINMAX
#include "SpineModel.h"

#include "SpineVersionDetector.h"

#include <glad/glad.h>
#include "stb_image.h"
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <utility>
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

std::wstring executableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
}

std::string windowsErrorMessage(DWORD error) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    if (!length || !message) return "Windows error " + std::to_string(error);
    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(length),
        nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(0, utf8Length)), '\0');
    if (utf8Length > 0)
        WideCharToMultiByte(CP_UTF8, 0, message, static_cast<int>(length), result.data(),
            utf8Length, nullptr, nullptr);
    LocalFree(message);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
        result.pop_back();
    return result;
}

GLint toMinFilter(int filter) {
    switch (filter) {
    case 1: return GL_NEAREST;
    case 4: return GL_NEAREST_MIPMAP_NEAREST;
    case 5: return GL_LINEAR_MIPMAP_NEAREST;
    case 6: return GL_NEAREST_MIPMAP_LINEAR;
    case 3:
    case 7: return GL_LINEAR_MIPMAP_LINEAR;
    default: return GL_LINEAR;
    }
}

GLint toMagFilter(int filter) { return filter == 1 ? GL_NEAREST : GL_LINEAR; }

GLint toWrap(int wrap) {
    switch (wrap) {
    case 0: return GL_MIRRORED_REPEAT;
    case 2: return GL_REPEAT;
    default: return GL_CLAMP_TO_EDGE;
    }
}

std::uint64_t createTexture(void*, const char* path, int minFilter, int magFilter,
                            int uWrap, int vWrap, int* width, int* height) {
    if (width) *width = 0;
    if (height) *height = 0;
    const std::wstring widePath = toWidePath(path);
    FILE* file = nullptr;
    if (!widePath.empty()) _wfopen_s(&file, widePath.c_str(), L"rb");
    if (!file) return 0;
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    const auto length = _ftelli64(file);
    if (length <= 0 || length > INT_MAX || _fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    const bool read = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if (!read) return 0;

    int imageWidth = 0;
    int imageHeight = 0;
    int components = 0;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
        &imageWidth, &imageHeight, &components, 4);
    if (!pixels) return 0;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageWidth, imageHeight, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, toMinFilter(minFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, toMagFilter(magFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, toWrap(uWrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, toWrap(vWrap));
    if (minFilter >= 3 && minFilter <= 7) glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(pixels);
    if (width) *width = imageWidth;
    if (height) *height = imageHeight;
    return texture;
}

void disposeTexture(void*, std::uint64_t texture) {
    const GLuint id = static_cast<GLuint>(texture);
    if (id) glDeleteTextures(1, &id);
}

std::wstring backendFilename(const SpineDataVersion& version) {
    if ((version.major == 3 && version.minor == 8) ||
        (version.major == 4 && version.minor >= 1 && version.minor <= 3))
        return L"SpineBackend" + std::to_wstring(version.major) +
            std::to_wstring(version.minor) + L".dll";
    return {};
}

std::string utf8(const std::wstring& value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace

SpineModel::~SpineModel() { unload(); }

bool SpineModel::load(const char* atlasPath, const char* skelPath) {
    lastError_.clear();
    const SpineDataVersion version = detectSpineDataVersion(skelPath);
    if (!version.valid()) {
        lastError_ = version.error;
        return false;
    }
    const std::wstring filename = backendFilename(version);
    if (filename.empty()) {
        lastError_ = "Unsupported Spine data version " + version.full +
            ". Installed backends support 3.8, 4.1, 4.2, and 4.3.";
        return false;
    }

    const std::wstring dllPath = executableDirectory() + L"\\runtimes\\" + filename;
    HMODULE newModule = LoadLibraryW(dllPath.c_str());
    if (!newModule) {
        lastError_ = "The Spine " + version.family() + " runtime backend could not be loaded (" +
            windowsErrorMessage(GetLastError()) + "). Expected DLL: runtimes\\" + utf8(filename);
        return false;
    }

    const auto getApi = reinterpret_cast<SpineBackendGetApi>(
        GetProcAddress(newModule, "SpineBackend_GetApi"));
    const SpineBackendApi* newApi = getApi ? getApi() : nullptr;
    if (!newApi || newApi->structSize < sizeof(SpineBackendApi) ||
        newApi->apiVersion != SPINE_BACKEND_API_VERSION ||
        newApi->runtimeMajor != version.major || newApi->runtimeMinor != version.minor) {
        lastError_ = "The selected Spine runtime DLL has an incompatible backend API.";
        FreeLibrary(newModule);
        return false;
    }

    SpineBackendHost host = {};
    host.structSize = sizeof(host);
    host.createTexture = createTexture;
    host.disposeTexture = disposeTexture;
    SpineBackendModelHandle newModel = newApi->create(&host);
    if (!newModel) {
        lastError_ = "Failed to create the Spine runtime backend.";
        FreeLibrary(newModule);
        return false;
    }
    if (!newApi->load(newModel, atlasPath, skelPath)) {
        const char* error = newApi->getLastError(newModel);
        lastError_ = error && *error ? error : "The Spine backend could not load this model.";
        newApi->destroy(newModel);
        FreeLibrary(newModule);
        return false;
    }

    unload();
    module_ = newModule;
    api_ = newApi;
    backendModel_ = newModel;
    api_->setEventCallback(backendModel_, dispatchEvent, this);
    return true;
}

void SpineModel::unload() {
    renderData_ = {};
    if (api_ && backendModel_) api_->destroy(backendModel_);
    backendModel_ = nullptr;
    api_ = nullptr;
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
    module_ = nullptr;
}

bool SpineModel::setAnimation(const char* name, bool loop, int trackIndex) {
    return api_ && backendModel_ && api_->setAnimation(backendModel_, name, loop ? 1 : 0, trackIndex) != 0;
}

bool SpineModel::queueAnimation(const char* name, bool loop, float delay, int trackIndex) {
    return api_ && backendModel_ && api_->queueAnimation(backendModel_, name, loop ? 1 : 0, delay, trackIndex) != 0;
}

void SpineModel::clearTrack(int trackIndex) { if (api_ && backendModel_) api_->clearTrack(backendModel_, trackIndex); }
void SpineModel::clearTracks() { if (api_ && backendModel_) api_->clearTracks(backendModel_); }
void SpineModel::setDefaultMix(float seconds) { if (api_ && backendModel_) api_->setDefaultMix(backendModel_, seconds); }
void SpineModel::setTimeScale(float scale) { if (api_ && backendModel_) api_->setTimeScale(backendModel_, scale); }
void SpineModel::setCurrentLoop(bool loop, int trackIndex) { if (api_ && backendModel_) api_->setCurrentLoop(backendModel_, loop ? 1 : 0, trackIndex); }
float SpineModel::timeScale() const { return api_ && backendModel_ ? api_->getTimeScale(backendModel_) : 0.0f; }
void SpineModel::update(float deltaTime) { if (api_ && backendModel_) api_->update(backendModel_, deltaTime); }
void SpineModel::applyAndUpdateWorldTransform() { if (api_ && backendModel_) api_->apply(backendModel_); }
void SpineModel::setPosition(float x, float y) { if (api_ && backendModel_) api_->setPosition(backendModel_, x, y); }
void SpineModel::setScale(float scale) { if (api_ && backendModel_) api_->setScale(backendModel_, scale); }
bool SpineModel::setSkin(const char* name) { return api_ && backendModel_ && api_->setSkin(backendModel_, name) != 0; }
void SpineModel::resetToSetupPose() { if (api_ && backendModel_) api_->resetToSetupPose(backendModel_); }

void SpineModel::setEventCallback(EventCallback callback) {
    eventCallback_ = std::move(callback);
    if (api_ && backendModel_) api_->setEventCallback(backendModel_, dispatchEvent, this);
}

bool SpineModel::buildRenderData(bool premultipliedAlpha) {
    renderData_ = {};
    return api_ && backendModel_ &&
        api_->buildRenderData(backendModel_, premultipliedAlpha ? 1 : 0, &renderData_) != 0;
}

std::vector<std::string> SpineModel::getAnimationNames() const {
    std::vector<std::string> names;
    if (!api_ || !backendModel_) return names;
    const int count = api_->getAnimationCount(backendModel_);
    names.reserve(std::max(0, count));
    for (int i = 0; i < count; ++i) {
        const char* name = api_->getAnimationName(backendModel_, i);
        if (name) names.emplace_back(name);
    }
    return names;
}

std::vector<std::string> SpineModel::getSkinNames() const {
    std::vector<std::string> names;
    if (!api_ || !backendModel_) return names;
    const int count = api_->getSkinCount(backendModel_);
    names.reserve(std::max(0, count));
    for (int i = 0; i < count; ++i) {
        const char* name = api_->getSkinName(backendModel_, i);
        if (name) names.emplace_back(name);
    }
    return names;
}

std::string SpineModel::dataVersion() const {
    const char* version = api_ && backendModel_ ? api_->getDataVersion(backendModel_) : nullptr;
    return version ? version : "";
}

std::string SpineModel::runtimeVersion() const {
    return api_ ? std::to_string(api_->runtimeMajor) + "." + std::to_string(api_->runtimeMinor) : "";
}

void SpineModel::dispatchEvent(void* userData, const SpineBackendEvent* event) {
    auto* model = static_cast<SpineModel*>(userData);
    if (!model || !event || !model->eventCallback_) return;
    SpineEventInfo info;
    info.name = event->name ? event->name : "";
    info.intValue = event->intValue;
    info.floatValue = event->floatValue;
    info.stringValue = event->stringValue ? event->stringValue : "";
    info.audioPath = event->audioPath ? event->audioPath : "";
    info.volume = event->volume;
    info.balance = event->balance;
    model->eventCallback_(info);
}
