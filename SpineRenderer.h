#pragma once

#include <vector>

struct spSkeleton;
struct spSkeletonClipping;

class SpineRenderer {
public:
    SpineRenderer() = default;
    ~SpineRenderer();

    SpineRenderer(const SpineRenderer&) = delete;
    SpineRenderer& operator=(const SpineRenderer&) = delete;

    bool initialize(unsigned int shaderProgram);
    void shutdown();
    void draw(spSkeleton* skeleton, const float* projection);
    bool getBounds(float& minX, float& minY, float& maxX, float& maxY) const;

    void setPremultipliedAlpha(bool enabled) { premultipliedAlpha_ = enabled; }
    bool premultipliedAlpha() const { return premultipliedAlpha_; }

private:
    void applyBlendMode(int blendMode) const;

    unsigned int shaderProgram_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    int projectionLocation_ = -1;
    int textureLocation_ = -1;
    bool premultipliedAlpha_ = true;
    bool boundsValid_ = false;
    float minX_ = 0.0f;
    float minY_ = 0.0f;
    float maxX_ = 0.0f;
    float maxY_ = 0.0f;
    spSkeletonClipping* clipper_ = nullptr;
    std::vector<float> worldVertices_;
    std::vector<float> vertexData_;
};
