#pragma once

#include <string>
#include <vector>

class SpineModel;

struct ModelConfig {
    std::string name;
    std::string atlasPath;
    std::string skeletonPath;
};

struct AppState {
    SpineModel* model = nullptr;
    std::vector<ModelConfig> models;
    int currentModelIndex = -1;
    std::vector<std::string> animations;
    std::string currentAnimation;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float scale = 1.0f;
    bool quit = false;
};
