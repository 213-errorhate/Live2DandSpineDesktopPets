#pragma once

#include "SpineBackendApi.h"

#include <functional>
#include <string>
#include <vector>

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
    bool buildRenderData(bool premultipliedAlpha);

    std::vector<std::string> getAnimationNames() const;
    std::vector<std::string> getSkinNames() const;
    std::string dataVersion() const;
    std::string runtimeVersion() const;
    const std::string& lastError() const { return lastError_; }
    const SpineBackendRenderData& renderData() const { return renderData_; }
    bool loaded() const { return backendModel_ != nullptr; }

private:
    static void dispatchEvent(void* userData, const SpineBackendEvent* event);

    void* module_ = nullptr;
    const SpineBackendApi* api_ = nullptr;
    SpineBackendModelHandle backendModel_ = nullptr;
    SpineBackendRenderData renderData_ = {};
    EventCallback eventCallback_;
    std::string lastError_;
};
