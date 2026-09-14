#pragma once

#include <string>
#include <vector>

class SpineModel;
class Live2DModel;

enum class PetModelType {
    Spine,
    Live2D
};

struct ModelConfig {
    std::string name;
    std::string atlasPath;
    std::string skeletonPath;
    bool premultipliedAlpha = true;
    bool managedFiles = false;
    PetModelType type = PetModelType::Spine;
};

struct AppState {
    SpineModel* model = nullptr;
    Live2DModel* live2dModel = nullptr;
    PetModelType currentModelType = PetModelType::Spine;
    std::vector<ModelConfig> models;
    int currentModelIndex = -1;
    std::vector<std::string> animations;
    std::string currentAnimation;
    std::vector<std::string> overlayAnimations;
    std::vector<std::string> skins;
    std::string currentSkin;
    bool animationLoop = true;
    bool premultipliedAlpha = true;
    float animationSpeed = 1.0f;
    float animationMix = 0.2f;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float scale = 1.0f;
    bool alwaysOnTop = false;
    bool alwaysOnTopChanged = false;
    bool mouseDragEnabled = true;
    unsigned int renderWidth = 1;
    unsigned int renderHeight = 1;
    bool quit = false;
};
