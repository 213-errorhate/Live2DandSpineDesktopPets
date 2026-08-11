#include "SpineModel.h"

#include <iostream>

extern "C" {
#include <spine/spine.h>
}

bool SpineModel::load(const char* atlasPath, const char* skelPath) {
    unload();

    atlas_ = spAtlas_createFromFile(atlasPath, 0);
    if (!atlas_) {
        std::cerr << "Failed to load atlas: " << atlasPath << std::endl;
        return false;
    }

    std::string path(skelPath);
    bool json = path.size() > 5 && path.substr(path.size() - 5) == ".json";
    if (json) {
        json_ = spSkeletonJson_create(atlas_);
        json_->scale = 1.0f;
        data_ = spSkeletonJson_readSkeletonDataFile(json_, skelPath);
    } else {
        binary_ = spSkeletonBinary_create(atlas_);
        binary_->scale = 1.0f;
        data_ = spSkeletonBinary_readSkeletonDataFile(binary_, skelPath);
    }
    if (!data_) {
        std::cerr << "Failed to load skeleton: "
                  << (json ? json_->error : binary_->error) << std::endl;
        unload();
        return false;
    }

    skeleton_ = spSkeleton_create(data_);
    animationStateData_ = spAnimationStateData_create(data_);
    animationState_ = spAnimationState_create(animationStateData_);
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

bool SpineModel::setAnimation(const char* name, bool loop) {
    if (!animationState_ || !name) return false;
    return spAnimationState_setAnimationByName(animationState_, 0, name, loop ? 1 : 0) != nullptr;
}

void SpineModel::update(float deltaTime) {
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

std::vector<std::string> SpineModel::getAnimationNames() const {
    std::vector<std::string> names;
    if (data_) {
        names.reserve(data_->animationsCount);
        for (int i = 0; i < data_->animationsCount; ++i)
            names.push_back(data_->animations[i]->name);
    }
    return names;
}
