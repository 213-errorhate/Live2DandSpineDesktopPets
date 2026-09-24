#include <windows.h>

#include "SpineBackendApi.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <spine/spine.h>
}

#ifndef SPINE_RUNTIME_MAJOR
#error SPINE_RUNTIME_MAJOR must be defined by the versioned backend project.
#endif
#ifndef SPINE_RUNTIME_MINOR
#error SPINE_RUNTIME_MINOR must be defined by the versioned backend project.
#endif

#define SPINE_STRINGIFY_DETAIL(value) #value
#define SPINE_STRINGIFY(value) SPINE_STRINGIFY_DETAIL(value)

namespace {

constexpr unsigned short kQuadIndices[] = {0, 1, 2, 2, 3, 0};
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

bool hasJsonExtension(const char* path) {
    if (!path) return false;
    std::string extension(path);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension.size() >= 5 && extension.compare(extension.size() - 5, 5, ".json") == 0;
}

struct BackendModel {
    spAtlas* atlas = nullptr;
    spSkeletonBinary* binary = nullptr;
    spSkeletonJson* json = nullptr;
    spSkeletonData* data = nullptr;
    spSkeleton* skeleton = nullptr;
    spAnimationStateData* animationStateData = nullptr;
    spAnimationState* animationState = nullptr;
    spSkeletonClipping* clipper = nullptr;
    SpineBackendEventCallback eventCallback = nullptr;
    void* eventUserData = nullptr;
    std::string lastError;
    std::vector<float> worldVertices;
    std::vector<SpineBackendVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<SpineBackendBatch> batches;

    BackendModel() : clipper(spSkeletonClipping_create()) {}

    ~BackendModel() {
        unload();
        if (clipper) spSkeletonClipping_dispose(clipper);
    }

    void unload() {
        if (animationState) spAnimationState_dispose(animationState);
        if (animationStateData) spAnimationStateData_dispose(animationStateData);
        if (skeleton) spSkeleton_dispose(skeleton);
        if (data) spSkeletonData_dispose(data);
        if (json) spSkeletonJson_dispose(json);
        if (binary) spSkeletonBinary_dispose(binary);
        if (atlas) spAtlas_dispose(atlas);
        animationState = nullptr;
        animationStateData = nullptr;
        skeleton = nullptr;
        data = nullptr;
        json = nullptr;
        binary = nullptr;
        atlas = nullptr;
        vertices.clear();
        indices.clear();
        batches.clear();
    }
};

BackendModel* castModel(SpineBackendModelHandle handle) {
    return static_cast<BackendModel*>(handle);
}

void animationListener(spAnimationState* state, spEventType type, spTrackEntry*, spEvent* event) {
    if (!state || type != SP_ANIMATION_EVENT || !event) return;
    auto* model = static_cast<BackendModel*>(state->rendererObject);
    if (!model || !model->eventCallback) return;
    SpineBackendEvent info = {};
    info.name = event->data && event->data->name ? event->data->name : "";
    info.intValue = event->intValue;
    info.floatValue = event->floatValue;
    info.stringValue = event->stringValue ? event->stringValue : "";
    info.audioPath = event->data && event->data->audioPath ? event->data->audioPath : "";
    info.volume = event->volume;
    info.balance = event->balance;
    model->eventCallback(model->eventUserData, &info);
}

SpineBackendModelHandle createModel(const SpineBackendHost* host) {
    if (!host || host->structSize < sizeof(SpineBackendHost) ||
        !host->createTexture || !host->disposeTexture)
        return nullptr;
    gHost = *host;
    return new BackendModel();
}

void destroyModel(SpineBackendModelHandle handle) {
    delete castModel(handle);
}

const char* getLastError(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model ? model->lastError.c_str() : "Invalid Spine backend model.";
}

int loadModel(SpineBackendModelHandle handle, const char* atlasPath, const char* skeletonPath) {
    auto* model = castModel(handle);
    if (!model || !atlasPath || !*atlasPath || !skeletonPath || !*skeletonPath) return 0;
    model->unload();
    model->lastError.clear();

    model->atlas = spAtlas_createFromFile(atlasPath, nullptr);
    if (!model->atlas) {
        model->lastError = std::string("Failed to load atlas: ") + atlasPath;
        return 0;
    }
    for (spAtlasPage* page = model->atlas->pages; page; page = page->next) {
        if (!page->rendererObject) {
            model->lastError = std::string("Failed to load atlas texture: ") +
                (page->name ? page->name : "unknown");
            model->unload();
            return 0;
        }
    }

    if (hasJsonExtension(skeletonPath)) {
        model->json = spSkeletonJson_create(model->atlas);
        if (model->json) {
            model->json->scale = 1.0f;
            model->data = spSkeletonJson_readSkeletonDataFile(model->json, skeletonPath);
        }
        if (!model->data)
            model->lastError = std::string("Failed to load skeleton: ") +
                (model->json && model->json->error ? model->json->error : "unknown JSON parse error");
    } else {
        model->binary = spSkeletonBinary_create(model->atlas);
        if (model->binary) {
            model->binary->scale = 1.0f;
            model->data = spSkeletonBinary_readSkeletonDataFile(model->binary, skeletonPath);
        }
        if (!model->data)
            model->lastError = std::string("Failed to load skeleton: ") +
                (model->binary && model->binary->error ? model->binary->error : "unknown binary parse error");
    }
    if (!model->data) {
        const std::string error = model->lastError;
        model->unload();
        model->lastError = error;
        return 0;
    }

    model->skeleton = spSkeleton_create(model->data);
    model->animationStateData = spAnimationStateData_create(model->data);
    model->animationState = model->animationStateData
        ? spAnimationState_create(model->animationStateData) : nullptr;
    if (!model->skeleton || !model->animationStateData || !model->animationState) {
        model->lastError = "Failed to allocate Spine runtime objects.";
        const std::string error = model->lastError;
        model->unload();
        model->lastError = error;
        return 0;
    }
    model->animationState->rendererObject = model;
    model->animationState->listener = animationListener;
    return 1;
}

int setAnimation(SpineBackendModelHandle handle, const char* name, int loop, int trackIndex) {
    auto* model = castModel(handle);
    return model && model->animationState && name &&
        spAnimationState_setAnimationByName(model->animationState, trackIndex, name, loop) ? 1 : 0;
}

int queueAnimation(SpineBackendModelHandle handle, const char* name, int loop, float delay, int trackIndex) {
    auto* model = castModel(handle);
    return model && model->animationState && name &&
        spAnimationState_addAnimationByName(model->animationState, trackIndex, name, loop, delay) ? 1 : 0;
}

void clearTrack(SpineBackendModelHandle handle, int trackIndex) {
    auto* model = castModel(handle);
    if (model && model->animationState && trackIndex >= 0)
        spAnimationState_clearTrack(model->animationState, trackIndex);
}

void clearTracks(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    if (model && model->animationState) spAnimationState_clearTracks(model->animationState);
}

void setDefaultMix(SpineBackendModelHandle handle, float seconds) {
    auto* model = castModel(handle);
    if (model && model->animationStateData)
        model->animationStateData->defaultMix = std::max(0.0f, seconds);
}

void setTimeScale(SpineBackendModelHandle handle, float scale) {
    auto* model = castModel(handle);
    if (model && model->animationState) model->animationState->timeScale = std::max(0.0f, scale);
}

void setCurrentLoop(SpineBackendModelHandle handle, int loop, int trackIndex) {
    auto* model = castModel(handle);
    if (!model || !model->animationState) return;
    spTrackEntry* entry = spAnimationState_getCurrent(model->animationState, trackIndex);
    if (entry) entry->loop = loop;
}

float getTimeScale(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model && model->animationState ? model->animationState->timeScale : 0.0f;
}

void update(SpineBackendModelHandle handle, float deltaTime) {
    auto* model = castModel(handle);
    if (!model) return;
#if SPINE_RUNTIME_MAJOR < 4 || (SPINE_RUNTIME_MAJOR == 4 && SPINE_RUNTIME_MINOR >= 2)
    if (model->skeleton) spSkeleton_update(model->skeleton, deltaTime);
#endif
    if (model->animationState) spAnimationState_update(model->animationState, deltaTime);
}

void apply(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    if (!model || !model->animationState || !model->skeleton) return;
    spAnimationState_apply(model->animationState, model->skeleton);
#if SPINE_RUNTIME_MAJOR > 4 || (SPINE_RUNTIME_MAJOR == 4 && SPINE_RUNTIME_MINOR >= 2)
    spSkeleton_updateWorldTransform(model->skeleton, SP_PHYSICS_UPDATE);
#else
    spSkeleton_updateWorldTransform(model->skeleton);
#endif
}

void setPosition(SpineBackendModelHandle handle, float x, float y) {
    auto* model = castModel(handle);
    if (model && model->skeleton) {
        model->skeleton->x = x;
        model->skeleton->y = y;
    }
}

void setScale(SpineBackendModelHandle handle, float scale) {
    auto* model = castModel(handle);
    if (model && model->skeleton) {
        model->skeleton->scaleX = scale;
        model->skeleton->scaleY = scale;
    }
}

int setSkin(SpineBackendModelHandle handle, const char* name) {
    auto* model = castModel(handle);
    if (!model || !model->skeleton) return 0;
    int result = 1;
    if (name && *name) result = spSkeleton_setSkinByName(model->skeleton, name);
    else spSkeleton_setSkin(model->skeleton, nullptr);
    if (result) spSkeleton_setSlotsToSetupPose(model->skeleton);
    return result ? 1 : 0;
}

void resetToSetupPose(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    if (model && model->skeleton) spSkeleton_setToSetupPose(model->skeleton);
}

void setEventCallback(SpineBackendModelHandle handle, SpineBackendEventCallback callback, void* userData) {
    auto* model = castModel(handle);
    if (!model) return;
    model->eventCallback = callback;
    model->eventUserData = userData;
}

int getAnimationCount(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model && model->data ? model->data->animationsCount : 0;
}

const char* getAnimationName(SpineBackendModelHandle handle, int index) {
    auto* model = castModel(handle);
    return model && model->data && index >= 0 && index < model->data->animationsCount &&
        model->data->animations[index] ? model->data->animations[index]->name : nullptr;
}

int getSkinCount(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model && model->data ? model->data->skinsCount : 0;
}

const char* getSkinName(SpineBackendModelHandle handle, int index) {
    auto* model = castModel(handle);
    return model && model->data && index >= 0 && index < model->data->skinsCount &&
        model->data->skins[index] ? model->data->skins[index]->name : nullptr;
}

const char* getDataVersion(SpineBackendModelHandle handle) {
    auto* model = castModel(handle);
    return model && model->data ? model->data->version : nullptr;
}

int buildRenderData(SpineBackendModelHandle handle, int premultipliedAlpha,
                    SpineBackendRenderData* renderData) {
    auto* model = castModel(handle);
    if (!model || !model->skeleton || !model->clipper || !renderData) return 0;
    model->vertices.clear();
    model->indices.clear();
    model->batches.clear();
    spSkeleton* skeleton = model->skeleton;

    for (int i = 0; i < skeleton->slotsCount; ++i) {
        spSlot* slot = skeleton->drawOrder[i];
        spAttachment* attachment = slot ? slot->attachment : nullptr;
        if (!attachment) {
            if (slot) spSkeletonClipping_clipEnd(model->clipper, slot);
            continue;
        }
        if (attachment->type == SP_ATTACHMENT_CLIPPING) {
            spSkeletonClipping_clipStart(model->clipper, slot,
                reinterpret_cast<spClippingAttachment*>(attachment));
            continue;
        }
        if (!slot->bone->active || slot->color.a <= 0.0f) {
            spSkeletonClipping_clipEnd(model->clipper, slot);
            continue;
        }

        float* positions = nullptr;
        float* uvs = nullptr;
        unsigned short* sourceIndices = nullptr;
        int vertexCount = 0;
        int indexCount = 0;
        spColor* attachmentColor = nullptr;
        std::uint64_t texture = 0;

        if (attachment->type == SP_ATTACHMENT_REGION) {
            auto* region = reinterpret_cast<spRegionAttachment*>(attachment);
            model->worldVertices.resize(8);
#if SPINE_RUNTIME_MAJOR >= 4
            spRegionAttachment_computeWorldVertices(region, slot,
                model->worldVertices.data(), 0, 2);
#else
            spRegionAttachment_computeWorldVertices(region, slot->bone,
                model->worldVertices.data(), 0, 2);
#endif
            positions = model->worldVertices.data();
            uvs = region->uvs;
            sourceIndices = const_cast<unsigned short*>(kQuadIndices);
            vertexCount = 4;
            indexCount = 6;
            attachmentColor = &region->color;
            auto* atlasRegion = reinterpret_cast<spAtlasRegion*>(region->rendererObject);
            if (atlasRegion && atlasRegion->page)
                texture = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(atlasRegion->page->rendererObject));
        } else if (attachment->type == SP_ATTACHMENT_MESH) {
            auto* mesh = reinterpret_cast<spMeshAttachment*>(attachment);
            const int coordinateCount = mesh->super.worldVerticesLength;
            model->worldVertices.resize(static_cast<size_t>(coordinateCount));
            spVertexAttachment_computeWorldVertices(reinterpret_cast<spVertexAttachment*>(mesh),
                slot, 0, coordinateCount, model->worldVertices.data(), 0, 2);
            positions = model->worldVertices.data();
            uvs = mesh->uvs;
            sourceIndices = mesh->triangles;
            vertexCount = coordinateCount / 2;
            indexCount = mesh->trianglesCount;
            attachmentColor = &mesh->color;
            auto* atlasRegion = reinterpret_cast<spAtlasRegion*>(mesh->rendererObject);
            if (atlasRegion && atlasRegion->page)
                texture = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(atlasRegion->page->rendererObject));
        } else {
            spSkeletonClipping_clipEnd(model->clipper, slot);
            continue;
        }

        if (!positions || !uvs || !sourceIndices || !attachmentColor || !texture || attachmentColor->a <= 0.0f) {
            spSkeletonClipping_clipEnd(model->clipper, slot);
            continue;
        }
        if (spSkeletonClipping_isClipping(model->clipper)) {
            spSkeletonClipping_clipTriangles(model->clipper, positions, vertexCount * 2,
                sourceIndices, indexCount, uvs, 2);
            positions = model->clipper->clippedVertices->items;
            uvs = model->clipper->clippedUVs->items;
            sourceIndices = model->clipper->clippedTriangles->items;
            vertexCount = model->clipper->clippedVertices->size / 2;
            indexCount = model->clipper->clippedTriangles->size;
        }

        const float alpha = skeleton->color.a * slot->color.a * attachmentColor->a;
        float lightR = skeleton->color.r * slot->color.r * attachmentColor->r;
        float lightG = skeleton->color.g * slot->color.g * attachmentColor->g;
        float lightB = skeleton->color.b * slot->color.b * attachmentColor->b;
        float darkR = slot->darkColor ? skeleton->color.r * slot->darkColor->r : 0.0f;
        float darkG = slot->darkColor ? skeleton->color.g * slot->darkColor->g : 0.0f;
        float darkB = slot->darkColor ? skeleton->color.b * slot->darkColor->b : 0.0f;
        if (premultipliedAlpha) {
            lightR *= alpha;
            lightG *= alpha;
            lightB *= alpha;
            darkR *= alpha;
            darkG *= alpha;
            darkB *= alpha;
        }

        SpineBackendBatch batch = {};
        batch.texture = texture;
        batch.blendMode = static_cast<int>(slot->data->blendMode);
        batch.vertexOffset = static_cast<std::uint32_t>(model->vertices.size());
        batch.vertexCount = static_cast<std::uint32_t>(vertexCount);
        batch.indexOffset = static_cast<std::uint32_t>(model->indices.size());
        batch.indexCount = static_cast<std::uint32_t>(indexCount);
        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            SpineBackendVertex output = {};
            output.x = positions[vertex * 2];
            output.y = positions[vertex * 2 + 1];
            output.u = uvs[vertex * 2];
            output.v = uvs[vertex * 2 + 1];
            output.lightR = lightR;
            output.lightG = lightG;
            output.lightB = lightB;
            output.lightA = alpha;
            output.darkR = darkR;
            output.darkG = darkG;
            output.darkB = darkB;
            output.premultipliedAlpha = premultipliedAlpha ? 1.0f : 0.0f;
            model->vertices.push_back(output);
        }
        model->indices.insert(model->indices.end(), sourceIndices, sourceIndices + indexCount);
        model->batches.push_back(batch);
        spSkeletonClipping_clipEnd(model->clipper, slot);
    }
    spSkeletonClipping_clipEnd2(model->clipper);

    renderData->vertices = model->vertices.data();
    renderData->vertexCount = static_cast<std::uint32_t>(model->vertices.size());
    renderData->indices = model->indices.data();
    renderData->indexCount = static_cast<std::uint32_t>(model->indices.size());
    renderData->batches = model->batches.data();
    renderData->batchCount = static_cast<std::uint32_t>(model->batches.size());
    return 1;
}

const SpineBackendApi kApi = {
    sizeof(SpineBackendApi), SPINE_BACKEND_API_VERSION,
    SPINE_RUNTIME_MAJOR, SPINE_RUNTIME_MINOR,
    "Spine " SPINE_STRINGIFY(SPINE_RUNTIME_MAJOR) "." SPINE_STRINGIFY(SPINE_RUNTIME_MINOR),
    createModel, destroyModel, getLastError, loadModel,
    setAnimation, queueAnimation, clearTrack, clearTracks, setDefaultMix,
    setTimeScale, setCurrentLoop, getTimeScale, update, apply, setPosition,
    setScale, setSkin, resetToSetupPose, setEventCallback,
    getAnimationCount, getAnimationName, getSkinCount, getSkinName,
    getDataVersion, buildRenderData
};

} // namespace

extern "C" void _spAtlasPage_createTexture(spAtlasPage* page, const char* path) {
    page->rendererObject = nullptr;
    if (!gHost.createTexture) return;
    std::int32_t width = 0;
    std::int32_t height = 0;
    const std::uint64_t texture = gHost.createTexture(gHost.userData, path,
        static_cast<int>(page->minFilter), static_cast<int>(page->magFilter),
        static_cast<int>(page->uWrap), static_cast<int>(page->vWrap), &width, &height);
    page->width = width;
    page->height = height;
    page->rendererObject = reinterpret_cast<void*>(static_cast<std::uintptr_t>(texture));
}

extern "C" void _spAtlasPage_disposeTexture(spAtlasPage* page) {
    if (!page || !page->rendererObject || !gHost.disposeTexture) return;
    gHost.disposeTexture(gHost.userData,
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(page->rendererObject)));
    page->rendererObject = nullptr;
}

extern "C" char* _spUtil_readFile(const char* path, int* length) {
    if (!length) return nullptr;
    *length = 0;
    const std::wstring widePath = toWidePath(path);
    FILE* file = widePath.empty() ? nullptr : _wfopen(widePath.c_str(), L"rb");
    if (!file) return nullptr;
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        return nullptr;
    }
    const auto fileLength = _ftelli64(file);
    if (fileLength <= 0 || fileLength > INT_MAX || _fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return nullptr;
    }
    char* data = static_cast<char*>(std::malloc(static_cast<size_t>(fileLength)));
    if (!data) {
        fclose(file);
        return nullptr;
    }
    const bool ok = fread(data, 1, static_cast<size_t>(fileLength), file) == static_cast<size_t>(fileLength);
    fclose(file);
    if (!ok) {
        std::free(data);
        return nullptr;
    }
    *length = static_cast<int>(fileLength);
    return data;
}

SPINE_BACKEND_EXPORT const SpineBackendApi* SpineBackend_GetApi() {
    return &kApi;
}
