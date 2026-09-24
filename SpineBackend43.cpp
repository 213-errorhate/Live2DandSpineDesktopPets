#include <windows.h>

#include "SpineBackendApi.h"
#include <spine-c.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

SpineBackendHost gHost = {};

std::wstring toWidePath(const char* path) {
    if (!path) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, result.data(), size);
    result.pop_back();
    return result;
}

bool readFile(const char* path, std::vector<std::uint8_t>& bytes, bool terminate) {
    FILE* file = nullptr;
    const std::wstring widePath = toWidePath(path);
    if (!widePath.empty()) _wfopen_s(&file, widePath.c_str(), L"rb");
    if (!file) return false;
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const auto length = _ftelli64(file);
    if (length <= 0 || length > INT_MAX || _fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(length) + (terminate ? 1 : 0));
    const bool ok = fread(bytes.data(), 1, static_cast<size_t>(length), file) == static_cast<size_t>(length);
    fclose(file);
    if (ok && terminate) bytes.back() = 0;
    return ok;
}

std::string directoryOf(const char* path) {
    std::string value = path ? path : "";
    const size_t separator = value.find_last_of("\\/");
    return separator == std::string::npos ? std::string() : value.substr(0, separator + 1);
}

bool hasJsonExtension(const char* path) {
    std::string value = path ? path : "";
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.size() >= 5 && value.compare(value.size() - 5, 5, ".json") == 0;
}

void* loadTexture(const char* path) {
    if (!gHost.createTexture) return nullptr;
    int width = 0;
    int height = 0;
    const std::uint64_t texture = gHost.createTexture(gHost.userData, path,
        2, 2, 1, 1, &width, &height); // Linear filtering, clamp to edge.
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(texture));
}

void unloadTexture(void* texture) {
    if (texture && gHost.disposeTexture)
        gHost.disposeTexture(gHost.userData,
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texture)));
}

struct BackendModel {
    spine_atlas atlas = nullptr;
    spine_skeleton_data data = nullptr;
    spine_skeleton_drawable drawable = nullptr;
    SpineBackendEventCallback eventCallback = nullptr;
    void* eventUserData = nullptr;
    std::string lastError;
    std::vector<SpineBackendVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<SpineBackendBatch> batches;

    ~BackendModel() { unload(); }

    void unload() {
        if (drawable) spine_skeleton_drawable_dispose(drawable);
        if (data) spine_skeleton_data_dispose(data);
        if (atlas) spine_atlas_dispose(atlas);
        drawable = nullptr;
        data = nullptr;
        atlas = nullptr;
        vertices.clear();
        indices.clear();
        batches.clear();
    }
};

BackendModel* castModel(SpineBackendModelHandle handle) {
    return static_cast<BackendModel*>(handle);
}

void eventListener(spine_animation_state, spine_event_type type, spine_track_entry,
                   spine_event event, void* userData) {
    auto* model = static_cast<BackendModel*>(userData);
    if (!model || !model->eventCallback || type != SPINE_EVENT_TYPE_EVENT || !event) return;
    const spine_event_data data = spine_event_get_data(event);
    SpineBackendEvent output = {};
    output.name = data ? spine_event_data_get_name(data) : "";
    output.intValue = spine_event_get_int(event);
    output.floatValue = spine_event_get_float(event);
    output.stringValue = spine_event_get_string(event);
    output.audioPath = data ? spine_event_data_get_audio_path(data) : "";
    output.volume = spine_event_get_volume(event);
    output.balance = spine_event_get_balance(event);
    model->eventCallback(model->eventUserData, &output);
}

SpineBackendModelHandle createModel(const SpineBackendHost* host) {
    if (!host || host->structSize < sizeof(SpineBackendHost) ||
        !host->createTexture || !host->disposeTexture)
        return nullptr;
    gHost = *host;
    return new BackendModel();
}

void destroyModel(SpineBackendModelHandle handle) { delete castModel(handle); }

const char* getLastError(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model ? model->lastError.c_str() : "Invalid Spine 4.3 backend model.";
}

int loadModel(SpineBackendModelHandle handle, const char* atlasPath, const char* skeletonPath) {
    auto* model = castModel(handle);
    if (!model || !atlasPath || !skeletonPath) return 0;
    model->unload();
    model->lastError.clear();

    std::vector<std::uint8_t> atlasBytes;
    if (!readFile(atlasPath, atlasBytes, true)) {
        model->lastError = std::string("Failed to read atlas: ") + atlasPath;
        return 0;
    }
    const std::string atlasDirectory = directoryOf(atlasPath);
    spine_atlas_result atlasResult = spine_atlas_load_callback(
        reinterpret_cast<const char*>(atlasBytes.data()), atlasDirectory.c_str(),
        loadTexture, unloadTexture);
    model->atlas = atlasResult ? spine_atlas_result_get_atlas(atlasResult) : nullptr;
    if (!model->atlas) {
        const char* error = atlasResult ? spine_atlas_result_get_error(atlasResult) : nullptr;
        model->lastError = error && *error ? error : "Failed to parse Spine 4.3 atlas.";
        if (atlasResult) spine_atlas_result_dispose(atlasResult);
        return 0;
    }
    spine_atlas_result_dispose(atlasResult);

    std::vector<std::uint8_t> skeletonBytes;
    const bool json = hasJsonExtension(skeletonPath);
    if (!readFile(skeletonPath, skeletonBytes, json)) {
        model->lastError = std::string("Failed to read skeleton: ") + skeletonPath;
        model->unload();
        return 0;
    }
    spine_skeleton_data_result dataResult = json
        ? spine_skeleton_data_load_json(model->atlas,
            reinterpret_cast<const char*>(skeletonBytes.data()), skeletonPath)
        : spine_skeleton_data_load_binary(model->atlas, skeletonBytes.data(),
            static_cast<int>(skeletonBytes.size()), skeletonPath);
    model->data = dataResult ? spine_skeleton_data_result_get_data(dataResult) : nullptr;
    if (!model->data) {
        const char* error = dataResult ? spine_skeleton_data_result_get_error(dataResult) : nullptr;
        model->lastError = error && *error ? error : "Failed to parse Spine 4.3 skeleton data.";
        if (dataResult) spine_skeleton_data_result_dispose(dataResult);
        const std::string savedError = model->lastError;
        model->unload();
        model->lastError = savedError;
        return 0;
    }
    spine_skeleton_data_result_dispose(dataResult);
    model->drawable = spine_skeleton_drawable_create(model->data);
    if (!model->drawable) {
        model->lastError = "Failed to create Spine 4.3 drawable.";
        const std::string savedError = model->lastError;
        model->unload();
        model->lastError = savedError;
        return 0;
    }
    spine_animation_state_set_listener(
        spine_skeleton_drawable_get_animation_state(model->drawable), eventListener, model);
    return 1;
}

spine_animation_state animationState(BackendModel* model) {
    return model && model->drawable ? spine_skeleton_drawable_get_animation_state(model->drawable) : nullptr;
}

spine_skeleton skeleton(BackendModel* model) {
    return model && model->drawable ? spine_skeleton_drawable_get_skeleton(model->drawable) : nullptr;
}

int setAnimation(SpineBackendModelHandle handle, const char* name, int loop, int trackIndex) {
    auto* model = castModel(handle);
    const auto state = animationState(model);
    return state && name && spine_animation_state_set_animation_1(state,
        static_cast<size_t>(std::max(0, trackIndex)), name, loop != 0) ? 1 : 0;
}

int queueAnimation(SpineBackendModelHandle handle, const char* name, int loop, float delay, int trackIndex) {
    auto* model = castModel(handle);
    const auto state = animationState(model);
    return state && name && spine_animation_state_add_animation_1(state,
        static_cast<size_t>(std::max(0, trackIndex)), name, loop != 0, delay) ? 1 : 0;
}

void clearTrack(SpineBackendModelHandle handle, int trackIndex) {
    const auto state = animationState(castModel(handle));
    if (state && trackIndex >= 0) spine_animation_state_clear_track(state, static_cast<size_t>(trackIndex));
}

void clearTracks(SpineBackendModelHandle handle) {
    const auto state = animationState(castModel(handle));
    if (state) spine_animation_state_clear_tracks(state);
}

void setDefaultMix(SpineBackendModelHandle handle, float seconds) {
    auto* model = castModel(handle);
    if (model && model->drawable)
        spine_animation_state_data_set_default_mix(
            spine_skeleton_drawable_get_animation_state_data(model->drawable), std::max(0.0f, seconds));
}

void setTimeScale(SpineBackendModelHandle handle, float scale) {
    const auto state = animationState(castModel(handle));
    if (state) spine_animation_state_set_time_scale(state, std::max(0.0f, scale));
}

void setCurrentLoop(SpineBackendModelHandle handle, int loop, int trackIndex) {
    const auto state = animationState(castModel(handle));
    if (!state || trackIndex < 0) return;
    const auto entry = spine_animation_state_get_track(state, static_cast<size_t>(trackIndex));
    if (entry) spine_track_entry_set_loop(entry, loop != 0);
}

float getTimeScale(SpineBackendModelHandle handle) {
    const auto state = animationState(castModel(handle));
    return state ? spine_animation_state_get_time_scale(state) : 0.0f;
}

void update(SpineBackendModelHandle handle, float deltaTime) {
    auto* model = castModel(handle);
    if (model && model->drawable) spine_skeleton_drawable_update(model->drawable, deltaTime);
}

void apply(SpineBackendModelHandle) {}

void setPosition(SpineBackendModelHandle handle, float x, float y) {
    const auto value = skeleton(castModel(handle));
    if (value) spine_skeleton_set_position(value, x, y);
}

void setScale(SpineBackendModelHandle handle, float scale) {
    const auto value = skeleton(castModel(handle));
    if (value) spine_skeleton_set_scale(value, scale, scale);
}

int setSkin(SpineBackendModelHandle handle, const char* name) {
    auto* model = castModel(handle);
    const auto value = skeleton(model);
    if (!model || !value || !model->data) return 0;
    if (name && *name) {
        if (!spine_skeleton_data_find_skin(model->data, name)) return 0;
        spine_skeleton_set_skin_1(value, name);
    } else {
        spine_skeleton_set_skin_2(value, nullptr);
    }
    spine_skeleton_setup_pose_slots(value);
    return 1;
}

void resetToSetupPose(SpineBackendModelHandle handle) {
    const auto value = skeleton(castModel(handle));
    if (value) spine_skeleton_setup_pose(value);
}

void setEventCallback(SpineBackendModelHandle handle, SpineBackendEventCallback callback, void* userData) {
    auto* model = castModel(handle);
    if (!model) return;
    model->eventCallback = callback;
    model->eventUserData = userData;
}

int getAnimationCount(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model && model->data
        ? static_cast<int>(spine_array_animation_size(spine_skeleton_data_get_animations(model->data))) : 0;
}

const char* getAnimationName(SpineBackendModelHandle handle, int index) {
    auto* model = castModel(handle);
    if (!model || !model->data || index < 0) return nullptr;
    const auto animations = spine_skeleton_data_get_animations(model->data);
    if (static_cast<size_t>(index) >= spine_array_animation_size(animations)) return nullptr;
    return spine_animation_get_name(spine_array_animation_buffer(animations)[index]);
}

int getSkinCount(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model && model->data
        ? static_cast<int>(spine_array_skin_size(spine_skeleton_data_get_skins(model->data))) : 0;
}

const char* getSkinName(SpineBackendModelHandle handle, int index) {
    auto* model = castModel(handle);
    if (!model || !model->data || index < 0) return nullptr;
    const auto skins = spine_skeleton_data_get_skins(model->data);
    if (static_cast<size_t>(index) >= spine_array_skin_size(skins)) return nullptr;
    return spine_skin_get_name(spine_array_skin_buffer(skins)[index]);
}

const char* getDataVersion(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model && model->data ? spine_skeleton_data_get_version(model->data) : nullptr;
}

float colorByte(std::uint32_t color, int shift) {
    return static_cast<float>((color >> shift) & 0xffu) / 255.0f;
}

int buildRenderData(SpineBackendModelHandle handle, int premultipliedAlpha,
                    SpineBackendRenderData* renderData) {
    auto* model = castModel(handle);
    if (!model || !model->drawable || !renderData) return 0;
    model->vertices.clear();
    model->indices.clear();
    model->batches.clear();
    for (spine_render_command command = spine_skeleton_drawable_render(model->drawable);
         command; command = spine_render_command_get_next(command)) {
        const int vertexCount = spine_render_command_get_num_vertices(command);
        const int indexCount = spine_render_command_get_num_indices(command);
        const float* positions = spine_render_command_get_positions(command);
        const float* uvs = spine_render_command_get_uvs(command);
        const std::uint32_t* colors = spine_render_command_get_colors(command);
        const std::uint32_t* darkColors = spine_render_command_get_dark_colors(command);
        const std::uint16_t* sourceIndices = spine_render_command_get_indices(command);
        void* texture = spine_render_command_get_texture(command);
        if (!positions || !uvs || !colors || !sourceIndices || !texture ||
            vertexCount <= 0 || indexCount <= 0)
            continue;

        SpineBackendBatch batch = {};
        batch.texture = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texture));
        batch.blendMode = static_cast<int>(spine_render_command_get_blend_mode(command));
        batch.vertexOffset = static_cast<std::uint32_t>(model->vertices.size());
        batch.vertexCount = static_cast<std::uint32_t>(vertexCount);
        batch.indexOffset = static_cast<std::uint32_t>(model->indices.size());
        batch.indexCount = static_cast<std::uint32_t>(indexCount);
        for (int i = 0; i < vertexCount; ++i) {
            const std::uint32_t light = colors[i];
            const std::uint32_t dark = darkColors ? darkColors[i] : 0xff000000u;
            SpineBackendVertex vertex = {};
            vertex.x = positions[i * 2];
            vertex.y = positions[i * 2 + 1];
            vertex.u = uvs[i * 2];
            vertex.v = uvs[i * 2 + 1];
            vertex.lightR = colorByte(light, 16);
            vertex.lightG = colorByte(light, 8);
            vertex.lightB = colorByte(light, 0);
            vertex.lightA = colorByte(light, 24);
            vertex.darkR = colorByte(dark, 16);
            vertex.darkG = colorByte(dark, 8);
            vertex.darkB = colorByte(dark, 0);
            if (premultipliedAlpha) {
                vertex.lightR *= vertex.lightA;
                vertex.lightG *= vertex.lightA;
                vertex.lightB *= vertex.lightA;
                vertex.darkR *= vertex.lightA;
                vertex.darkG *= vertex.lightA;
                vertex.darkB *= vertex.lightA;
            }
            vertex.premultipliedAlpha = premultipliedAlpha ? 1.0f : 0.0f;
            model->vertices.push_back(vertex);
        }
        model->indices.insert(model->indices.end(), sourceIndices, sourceIndices + indexCount);
        model->batches.push_back(batch);
    }
    renderData->vertices = model->vertices.data();
    renderData->vertexCount = static_cast<std::uint32_t>(model->vertices.size());
    renderData->indices = model->indices.data();
    renderData->indexCount = static_cast<std::uint32_t>(model->indices.size());
    renderData->batches = model->batches.data();
    renderData->batchCount = static_cast<std::uint32_t>(model->batches.size());
    return 1;
}

const SpineBackendApi kApi = {
    sizeof(SpineBackendApi), SPINE_BACKEND_API_VERSION, 4, 3, "Spine 4.3",
    createModel, destroyModel, getLastError, loadModel,
    setAnimation, queueAnimation, clearTrack, clearTracks, setDefaultMix,
    setTimeScale, setCurrentLoop, getTimeScale, update, apply, setPosition,
    setScale, setSkin, resetToSetupPose, setEventCallback,
    getAnimationCount, getAnimationName, getSkinCount, getSkinName,
    getDataVersion, buildRenderData
};

} // namespace

SPINE_BACKEND_EXPORT const SpineBackendApi* SpineBackend_GetApi() { return &kApi; }
