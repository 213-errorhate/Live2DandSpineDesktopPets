#pragma once

#include <string>
#include <vector>

struct ModelConfig;

bool loadModelRegistry(const std::string& path, std::vector<ModelConfig>& models);
bool saveModelRegistry(const std::string& path, const std::vector<ModelConfig>& models);
bool isLive2DModelJson(const std::string& path);
bool discoverLive2DModels(const std::string& root, std::vector<ModelConfig>& models);
bool importModelFiles(const ModelConfig& source, const std::string& assetsRoot, ModelConfig& dest);
bool removeModelFiles(const ModelConfig& config);
bool isPathInsideDirectory(const std::string& path, const std::string& directory);
