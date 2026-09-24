#pragma once

#include <cstddef>
#include <cstdint>

// Stable C ABI shared by the desktop-pet executable and every versioned Spine
// runtime DLL. No Spine runtime types or C++ containers may cross this boundary.
constexpr std::uint32_t SPINE_BACKEND_API_VERSION = 1;

enum SpineBackendBlendMode : std::int32_t {
    SPINE_BACKEND_BLEND_NORMAL = 0,
    SPINE_BACKEND_BLEND_ADDITIVE = 1,
    SPINE_BACKEND_BLEND_MULTIPLY = 2,
    SPINE_BACKEND_BLEND_SCREEN = 3
};

struct SpineBackendVertex {
    float x;
    float y;
    float u;
    float v;
    float lightR;
    float lightG;
    float lightB;
    float lightA;
    float darkR;
    float darkG;
    float darkB;
    float premultipliedAlpha;
};

struct SpineBackendBatch {
    std::uint64_t texture;
    std::int32_t blendMode;
    std::uint32_t vertexOffset;
    std::uint32_t vertexCount;
    std::uint32_t indexOffset;
    std::uint32_t indexCount;
};

struct SpineBackendRenderData {
    const SpineBackendVertex* vertices;
    std::uint32_t vertexCount;
    const std::uint16_t* indices;
    std::uint32_t indexCount;
    const SpineBackendBatch* batches;
    std::uint32_t batchCount;
};

struct SpineBackendEvent {
    const char* name;
    std::int32_t intValue;
    float floatValue;
    const char* stringValue;
    const char* audioPath;
    float volume;
    float balance;
};

using SpineBackendTextureCreate = std::uint64_t (*)(
    void* userData, const char* path, std::int32_t minFilter,
    std::int32_t magFilter, std::int32_t uWrap, std::int32_t vWrap,
    std::int32_t* width, std::int32_t* height);
using SpineBackendTextureDispose = void (*)(void* userData, std::uint64_t texture);
using SpineBackendEventCallback = void (*)(void* userData, const SpineBackendEvent* event);

struct SpineBackendHost {
    std::uint32_t structSize;
    void* userData;
    SpineBackendTextureCreate createTexture;
    SpineBackendTextureDispose disposeTexture;
};

using SpineBackendModelHandle = void*;

struct SpineBackendApi {
    std::uint32_t structSize;
    std::uint32_t apiVersion;
    std::int32_t runtimeMajor;
    std::int32_t runtimeMinor;
    const char* runtimeName;

    SpineBackendModelHandle (*create)(const SpineBackendHost* host);
    void (*destroy)(SpineBackendModelHandle model);
    const char* (*getLastError)(SpineBackendModelHandle model);
    std::int32_t (*load)(SpineBackendModelHandle model, const char* atlasPath, const char* skeletonPath);

    std::int32_t (*setAnimation)(SpineBackendModelHandle model, const char* name, std::int32_t loop, std::int32_t trackIndex);
    std::int32_t (*queueAnimation)(SpineBackendModelHandle model, const char* name, std::int32_t loop, float delay, std::int32_t trackIndex);
    void (*clearTrack)(SpineBackendModelHandle model, std::int32_t trackIndex);
    void (*clearTracks)(SpineBackendModelHandle model);
    void (*setDefaultMix)(SpineBackendModelHandle model, float seconds);
    void (*setTimeScale)(SpineBackendModelHandle model, float scale);
    void (*setCurrentLoop)(SpineBackendModelHandle model, std::int32_t loop, std::int32_t trackIndex);
    float (*getTimeScale)(SpineBackendModelHandle model);
    void (*update)(SpineBackendModelHandle model, float deltaTime);
    void (*apply)(SpineBackendModelHandle model);
    void (*setPosition)(SpineBackendModelHandle model, float x, float y);
    void (*setScale)(SpineBackendModelHandle model, float scale);
    std::int32_t (*setSkin)(SpineBackendModelHandle model, const char* name);
    void (*resetToSetupPose)(SpineBackendModelHandle model);
    void (*setEventCallback)(SpineBackendModelHandle model, SpineBackendEventCallback callback, void* userData);

    std::int32_t (*getAnimationCount)(SpineBackendModelHandle model);
    const char* (*getAnimationName)(SpineBackendModelHandle model, std::int32_t index);
    std::int32_t (*getSkinCount)(SpineBackendModelHandle model);
    const char* (*getSkinName)(SpineBackendModelHandle model, std::int32_t index);
    const char* (*getDataVersion)(SpineBackendModelHandle model);
    std::int32_t (*buildRenderData)(SpineBackendModelHandle model, std::int32_t premultipliedAlpha,
                                    SpineBackendRenderData* renderData);
};

using SpineBackendGetApi = const SpineBackendApi* (*)();

#if defined(_WIN32)
#define SPINE_BACKEND_EXPORT extern "C" __declspec(dllexport)
#else
#define SPINE_BACKEND_EXPORT extern "C"
#endif

