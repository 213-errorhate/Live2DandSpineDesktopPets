#pragma once

#include <functional>
#include <string>
#include <vector>

struct spAtlas;
struct spSkeleton;
struct spSkeletonData;
struct spSkeletonBinary;
struct spSkeletonJson;
struct spAnimationState;
struct spAnimationStateData;

struct SpineEventInfo {
    std::string name;
    int intValue = 0;
    float floatValue = 0.0f;
    std::string stringValue;
    std::string audioPath;
    float volume = 0.0f;
    float balance = 0.0f;
};

class SpineModel {
public:
    using EventCallback = std::function<void(const SpineEventInfo&)>;

    SpineModel() = default;
    ~SpineModel();
    SpineModel(const SpineModel&) = delete;
    SpineModel& operator=(const SpineModel&) = delete;

    bool load(const char* atlasPath, const char* skelPath);
    void unload();

    bool setAnimation(const char* name, bool loop = true, int trackIndex = 0);
    bool queueAnimation(const char* name, bool loop = true, float delay = 0.0f,
                        int trackIndex = 0);
    void clearTrack(int trackIndex);
    void clearTracks();
    void setDefaultMix(float seconds);
    void setTimeScale(float scale);
    void setCurrentLoop(bool loop, int trackIndex = 0);
    float timeScale() const;
    void update(float deltaTime);
    void applyAndUpdateWorldTransform();
    void setPosition(float x, float y);
    void setScale(float scale);
    bool setSkin(const char* name);
    void resetToSetupPose();
    void setEventCallback(EventCallback callback);

    std::vector<std::string> getAnimationNames() const;
    std::vector<std::string> getSkinNames() const;
    std::string dataVersion() const;
    const std::string& lastError() const { return lastError_; }
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
    EventCallback eventCallback_;
    std::string lastError_;
};
