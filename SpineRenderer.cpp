#include "SpineRenderer.h"

#include <algorithm>
#include <cstddef>
#include <iostream>

#include <glad/glad.h>

SpineRenderer::~SpineRenderer() { shutdown(); }

bool SpineRenderer::initialize(unsigned int shaderProgram) {
    shutdown();
    if (!shaderProgram) return false;
    shaderProgram_ = shaderProgram;
    projectionLocation_ = glGetUniformLocation(shaderProgram_, "projection");
    textureLocation_ = glGetUniformLocation(shaderProgram_, "image");
    if (projectionLocation_ < 0 || textureLocation_ < 0) {
        std::cerr << "Spine shader is missing required uniforms." << std::endl;
        shutdown();
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    const GLsizei stride = sizeof(SpineBackendVertex);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SpineBackendVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SpineBackendVertex, u)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SpineBackendVertex, lightR)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(SpineBackendVertex, darkR)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);
    if (!vao_ || !vbo_ || !ebo_) {
        shutdown();
        return false;
    }
    return true;
}
void SpineRenderer::shutdown() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    ebo_ = 0;
    vbo_ = 0;
    vao_ = 0;
    shaderProgram_ = 0;
    projectionLocation_ = -1;
    textureLocation_ = -1;
    boundsValid_ = false;
}

bool SpineRenderer::getBounds(float& minX, float& minY, float& maxX, float& maxY) const {
    if (!boundsValid_) return false;
    minX = minX_;
    minY = minY_;
    maxX = maxX_;
    maxY = maxY_;
    return true;
}

void SpineRenderer::applyBlendMode(int blendMode) const {
    switch (blendMode) {
    case SPINE_BACKEND_BLEND_ADDITIVE:
        glBlendFunc(premultipliedAlpha_ ? GL_ONE : GL_SRC_ALPHA, GL_ONE);
        break;
    case SPINE_BACKEND_BLEND_MULTIPLY:
        glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case SPINE_BACKEND_BLEND_SCREEN:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
        break;
    default:
        glBlendFunc(premultipliedAlpha_ ? GL_ONE : GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

void SpineRenderer::draw(const SpineBackendRenderData& data, const float* projection) {
    boundsValid_ = false;
    if (!projection || !shaderProgram_ || !data.vertices || !data.indices || !data.batches) return;
    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, projection);
    glUniform1i(textureLocation_, 0);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBindVertexArray(vao_);

    for (std::uint32_t i = 0; i < data.batchCount; ++i) {
        const SpineBackendBatch& batch = data.batches[i];
        if (!batch.texture || !batch.vertexCount || !batch.indexCount ||
            batch.vertexOffset + batch.vertexCount > data.vertexCount ||
            batch.indexOffset + batch.indexCount > data.indexCount)
            continue;
        const SpineBackendVertex* vertices = data.vertices + batch.vertexOffset;
        const std::uint16_t* indices = data.indices + batch.indexOffset;
        for (std::uint32_t vertex = 0; vertex < batch.vertexCount; ++vertex) {
            const float x = vertices[vertex].x;
            const float y = vertices[vertex].y;
            if (!boundsValid_) {
                minX_ = maxX_ = x;
                minY_ = maxY_ = y;
                boundsValid_ = true;
            } else {
                minX_ = std::min(minX_, x);
                minY_ = std::min(minY_, y);
                maxX_ = std::max(maxX_, x);
                maxY_ = std::max(maxY_, y);
            }
        }

        applyBlendMode(batch.blendMode);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(batch.texture));
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(
            batch.vertexCount * sizeof(SpineBackendVertex)), vertices, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(
            batch.indexCount * sizeof(std::uint16_t)), indices, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, batch.indexCount, GL_UNSIGNED_SHORT, nullptr);
    }
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
