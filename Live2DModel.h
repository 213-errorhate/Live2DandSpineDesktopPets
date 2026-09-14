#pragma once

#include <memory>
#include <string>
#include <vector>

class Live2DModel {
public:
    Live2DModel();
    ~Live2DModel();

    Live2DModel(const Live2DModel&) = delete;
    Live2DModel& operator=(const Live2DModel&) = delete;

    static bool initializeFramework();
    static void shutdownFramework();
    static bool frameworkReady();

    bool load(const char* modelJsonPath, unsigned int renderWidth, unsigned int renderHeight);
    void unload();
    bool loaded() const;

    void update(float deltaTime);
    void draw(unsigned int renderWidth, unsigned int renderHeight,
              float positionX, float positionY, float scale);

    bool setAnimation(const char* name, bool loop = true);
    void setCurrentLoop(bool loop);
    void setTimeScale(float scale);
    bool setExpression(const char* name);

    std::vector<std::string> getAnimationNames() const;
    std::vector<std::string> getExpressionNames() const;
    bool getNdcBounds(float& minX, float& minY, float& maxX, float& maxY) const;
    const std::string& lastError() const { return lastError_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::string lastError_;
};
