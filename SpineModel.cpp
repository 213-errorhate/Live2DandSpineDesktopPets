#include "SpineModel.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <utility>

extern "C" {
#include <spine/spine.h>
}

namespace {

constexpr const char* kSupportedSpineVersion = "3.8";

bool hasJsonExtension(const char* path) {
    if (!path) return false;
    std::string extension(path);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension.size() >= 5 && extension.compare(extension.size() - 5, 5, ".json") == 0;
}

bool isSupportedVersion(const char* version) {
    if (!version) return false;
    const std::string value(version);
    return value == kSupportedSpineVersion || value.rfind(std::string(kSupportedSpineVersion) + ".", 0) == 0;
}

} // namespace

SpineModel::~SpineModel() {
    unload();
}

bool SpineModel::load(const char* atlasPath, const char* skelPath) {
    lastError_.clear();
    if (!atlasPath || !*atlasPath || !skelPath || !*skelPath) {
        lastError_ = "Atlas and skeleton paths are required.";
        return false;
    }

    spAtlas* newAtlas = spAtlas_createFromFile(atlasPath, nullptr);
    if (!newAtlas) {
        lastError_ = std::string("Failed to load atlas: ") + atlasPath;
        std::cerr << lastError_ << std::endl;
        return false;
    }

    for (spAtlasPage* page = newAtlas->pages; page; page = page->next) {
        if (!page->rendererObject) {
            lastError_ = std::string("Failed to load atlas texture: ") + (page->name ? page->name : "unknown");
            spAtlas_dispose(newAtlas);
            std::cerr << lastError_ << std::endl;
            return false;
        }
    }

    spSkeletonBinary* newBinary = nullptr;
    spSkeletonJson* newJson = nullptr;
    spSkeletonData* newData = nullptr;
    const bool json = hasJsonExtension(skelPath);
    if (json) {
        newJson = spSkeletonJson_create(newAtlas);
        if (!newJson) {
            lastError_ = "Failed to create Spine JSON reader.";
            spAtlas_dispose(newAtlas);
            return false;
        }
        newJson->scale = 1.0f;
        newData = spSkeletonJson_readSkeletonDataFile(newJson, skelPath);
    } else {
        newBinary = spSkeletonBinary_create(newAtlas);
        if (!newBinary) {
            lastError_ = "Failed to create Spine binary reader.";
            spAtlas_dispose(newAtlas);
            return false;
        }
        newBinary->scale = 1.0f;
        newData = spSkeletonBinary_readSkeletonDataFile(newBinary, skelPath);
    }

    if (!newData) {
        const char* runtimeError = json ? newJson->error : newBinary->error;
        lastError_ = std::string("Failed to load skeleton: ") +
            (runtimeError ? runtimeError : "unknown parse error");
        if (newJson) spSkeletonJson_dispose(newJson);
        if (newBinary) spSkeletonBinary_dispose(newBinary);
        spAtlas_dispose(newAtlas);
        std::cerr << lastError_ << std::endl;
        return false;
    }

    if (!isSupportedVersion(newData->version)) {
        lastError_ = "Unsupported Spine data version ";
        lastError_ += newData->version ? newData->version : "unknown";
        lastError_ += "; this build requires Spine 3.8 exports.";
        spSkeletonData_dispose(newData);
        if (newJson) spSkeletonJson_dispose(newJson);
        if (newBinary) spSkeletonBinary_dispose(newBinary);
        spAtlas_dispose(newAtlas);
        std::cerr << lastError_ << std::endl;
        return false;
    }

    spSkeleton* newSkeleton = spSkeleton_create(newData);
    spAnimationStateData* newStateData = spAnimationStateData_create(newData);
    spAnimationState* newState = spAnimationState_create(newStateData);
    if (!newSkeleton || !newStateData || !newState) {
        lastError_ = "Failed to allocate Spine runtime objects.";
        if (newState) spAnimationState_dispose(newState);
        if (newStateData) spAnimationStateData_dispose(newStateData);
        if (newSkeleton) spSkeleton_dispose(newSkeleton);
        spSkeletonData_dispose(newData);
        if (newJson) spSkeletonJson_dispose(newJson);
        if (newBinary) spSkeletonBinary_dispose(newBinary);
        spAtlas_dispose(newAtlas);
        return false;
    }

    unload();
    atlas_ = newAtlas;
    binary_ = newBinary;
    json_ = newJson;
    data_ = newData;
    skeleton_ = newSkeleton;
    animationStateData_ = newStateData;
    animationState_ = newState;
    animationState_->rendererObject = this;
    animationState_->listener = [](spAnimationState* state, spEventType type,
                                   spTrackEntry*, spEvent* event) {
        if (type != SP_ANIMATION_EVENT || !state || !event) return;
        auto* self = static_cast<SpineModel*>(state->rendererObject);
        if (!self || !self->eventCallback_) return;
        SpineEventInfo info;
        info.name = event->data && event->data->name ? event->data->name : "";
        info.intValue = event->intValue;
        info.floatValue = event->floatValue;
        info.stringValue = event->stringValue ? event->stringValue : "";
        info.audioPath = event->data && event->data->audioPath ? event->data->audioPath : "";
        info.volume = event->volume;
        info.balance = event->balance;
        self->eventCallback_(info);
    };
    return true;
}

void SpineModel::unload() {
    if (animationState_) spAnimationState_dispose(animationState_);
    if (animationStateData_) spAnimationStateData_dispose(animationStateData_);
    if (skeleton_) spSkeleton_dispose(skeleton_);
    if (data_) spSkeletonData_dispose(data_);
    if (json_) spSkeletonJson_dispose(json_);
    if (binary_) spSkeletonBinary_dispose(binary_);
    if (atlas_) spAtlas_dispose(atlas_);

    animationState_ = nullptr;
    animationStateData_ = nullptr;
    skeleton_ = nullptr;
    data_ = nullptr;
    json_ = nullptr;
    binary_ = nullptr;
    atlas_ = nullptr;
}

bool SpineModel::setAnimation(const char* name, bool loop, int trackIndex) {
    if (!animationState_ || !name) return false;
    return spAnimationState_setAnimationByName(
        animationState_, trackIndex, name, loop ? 1 : 0) != nullptr;
}

bool SpineModel::queueAnimation(const char* name, bool loop, float delay, int trackIndex) {
    if (!animationState_ || !name) return false;
    return spAnimationState_addAnimationByName(
        animationState_, trackIndex, name, loop ? 1 : 0, delay) != nullptr;
}

void SpineModel::clearTrack(int trackIndex) {
    if (animationState_ && trackIndex >= 0)
        spAnimationState_clearTrack(animationState_, trackIndex);
}

void SpineModel::clearTracks() {
    if (animationState_) spAnimationState_clearTracks(animationState_);
}

void SpineModel::setDefaultMix(float seconds) {
    if (animationStateData_)
        animationStateData_->defaultMix = std::max(0.0f, seconds);
}

void SpineModel::setTimeScale(float scale) {
    if (animationState_)
        animationState_->timeScale = std::max(0.0f, scale);
}

void SpineModel::setCurrentLoop(bool loop, int trackIndex) {
    if (!animationState_) return;
    spTrackEntry* entry = spAnimationState_getCurrent(animationState_, trackIndex);
    if (entry) entry->loop = loop ? 1 : 0;
}

float SpineModel::timeScale() const {
    return animationState_ ? animationState_->timeScale : 0.0f;
}

void SpineModel::update(float deltaTime) {
    if (skeleton_) spSkeleton_update(skeleton_, deltaTime);
    if (animationState_) spAnimationState_update(animationState_, deltaTime);
}

void SpineModel::applyAndUpdateWorldTransform() {
    if (animationState_ && skeleton_) {
        spAnimationState_apply(animationState_, skeleton_);
        spSkeleton_updateWorldTransform(skeleton_);
    }
}

void SpineModel::setPosition(float x, float y) {
    if (skeleton_) {
        skeleton_->x = x;
        skeleton_->y = y;
    }
}

void SpineModel::setScale(float scale) {
    if (skeleton_) {
        skeleton_->scaleX = scale;
        skeleton_->scaleY = scale;
    }
}

bool SpineModel::setSkin(const char* name) {
    if (!skeleton_) return false;
    int result = 1;
    if (name && *name)
        result = spSkeleton_setSkinByName(skeleton_, name);
    else
        spSkeleton_setSkin(skeleton_, nullptr);
    if (!result) return false;
    spSkeleton_setSlotsToSetupPose(skeleton_);
    return true;
}

void SpineModel::resetToSetupPose() {
    if (skeleton_) spSkeleton_setToSetupPose(skeleton_);
}

void SpineModel::setEventCallback(EventCallback callback) {
    eventCallback_ = std::move(callback);
}

std::vector<std::string> SpineModel::getAnimationNames() const {
    std::vector<std::string> names;
    if (data_) {
        names.reserve(data_->animationsCount);
        for (int i = 0; i < data_->animationsCount; ++i)
            names.push_back(data_->animations[i]->name);
    }
    return names;
}

std::vector<std::string> SpineModel::getSkinNames() const {
    std::vector<std::string> names;
    if (data_) {
        names.reserve(data_->skinsCount);
        for (int i = 0; i < data_->skinsCount; ++i) {
            if (data_->skins[i] && data_->skins[i]->name)
                names.emplace_back(data_->skins[i]->name);
        }
    }
    return names;
}

std::string SpineModel::dataVersion() const {
    return data_ && data_->version ? data_->version : "";
}
