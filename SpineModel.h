#pragma once

#include <string>
#include <vector>

struct spAtlas;
struct spSkeleton;
struct spSkeletonData;
struct spSkeletonBinary;
struct spSkeletonJson;
struct spAnimationState;
struct spAnimationStateData;

class SpineModel {
public:
    bool load(const char* atlasPath, const char* skelPath);
    void unload();

    bool setAnimation(const char* name, bool loop = true);
    void update(float deltaTime);
    void applyAndUpdateWorldTransform();
    void setPosition(float x, float y);
    void setScale(float scale);

    std::vector<std::string> getAnimationNames() const;
    spSkeleton* skeleton() const { return skeleton_; }
    spAnimationState* animationState() const { return animationState_; }
    bool loaded() const { return skeleton_ != nullptr; }

private:
    spAtlas* atlas_ = nullptr;
    spSkeletonBinary* binary_ = nullptr;
    spSkeletonJson* json_ = nullptr;
    spSkeletonData* data_ = nullptr;
    spSkeleton* skeleton_ = nullptr;
    spAnimationState* animationState_ = nullptr;
    spAnimationStateData* animationStateData_ = nullptr;
};
