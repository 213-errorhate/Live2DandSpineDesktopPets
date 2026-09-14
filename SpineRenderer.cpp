#include "SpineRenderer.h"

#include <algorithm>
#include <iostream>

#include <glad/glad.h>

extern "C" {
#include <spine/spine.h>
}

namespace {

constexpr int kFloatsPerVertex = 12;
const unsigned short kQuadIndices[] = { 0, 1, 2, 2, 3, 0 };

} // namespace

SpineRenderer::~SpineRenderer() {
    shutdown();
}

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

    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glBindVertexArray(0);

    clipper_ = spSkeletonClipping_create();
    if (vao_ == 0 || vbo_ == 0 || ebo_ == 0 || !clipper_) {
        std::cerr << "Failed to allocate Spine renderer resources." << std::endl;
        shutdown();
        return false;
    }
    return true;
}

void SpineRenderer::shutdown() {
    if (clipper_) {
        spSkeletonClipping_dispose(clipper_);
        clipper_ = nullptr;
    }
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
    worldVertices_.clear();
    vertexData_.clear();
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
    case SP_BLEND_MODE_ADDITIVE:
        glBlendFunc(premultipliedAlpha_ ? GL_ONE : GL_SRC_ALPHA, GL_ONE);
        break;
    case SP_BLEND_MODE_MULTIPLY:
        glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case SP_BLEND_MODE_SCREEN:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
        break;
    case SP_BLEND_MODE_NORMAL:
    default:
        glBlendFunc(premultipliedAlpha_ ? GL_ONE : GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

void SpineRenderer::draw(spSkeleton* skeleton, const float* projection) {
    boundsValid_ = false;
    if (!skeleton || !projection || !shaderProgram_ || !clipper_) return;
    if (skeleton->color.a <= 0.0f) return;

    glUseProgram(shaderProgram_);
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, projection);
    glUniform1i(textureLocation_, 0);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBindVertexArray(vao_);

    for (int i = 0; i < skeleton->slotsCount; ++i) {
        spSlot* slot = skeleton->drawOrder[i];
        spAttachment* attachment = slot ? slot->attachment : nullptr;
        if (!attachment) {
            if (slot) spSkeletonClipping_clipEnd(clipper_, slot);
            continue;
        }

        if (attachment->type == SP_ATTACHMENT_CLIPPING) {
            spSkeletonClipping_clipStart(
                clipper_, slot, reinterpret_cast<spClippingAttachment*>(attachment));
            continue;
        }

        if (!slot->bone->active || slot->color.a <= 0.0f) {
            spSkeletonClipping_clipEnd(clipper_, slot);
            continue;
        }

        float* positions = nullptr;
        float* uvs = nullptr;
        unsigned short* indices = nullptr;
        int vertexCount = 0;
        int indexCount = 0;
        spColor* attachmentColor = nullptr;
        unsigned int texture = 0;

        if (attachment->type == SP_ATTACHMENT_REGION) {
            auto* region = reinterpret_cast<spRegionAttachment*>(attachment);
            worldVertices_.resize(8);
            spRegionAttachment_computeWorldVertices(region, slot->bone, worldVertices_.data(), 0, 2);
            positions = worldVertices_.data();
            uvs = region->uvs;
            indices = const_cast<unsigned short*>(kQuadIndices);
            vertexCount = 4;
            indexCount = 6;
            attachmentColor = &region->color;
            auto* atlasRegion = reinterpret_cast<spAtlasRegion*>(region->rendererObject);
            if (atlasRegion && atlasRegion->page)
                texture = static_cast<unsigned int>(reinterpret_cast<size_t>(atlasRegion->page->rendererObject));
        } else if (attachment->type == SP_ATTACHMENT_MESH) {
            auto* mesh = reinterpret_cast<spMeshAttachment*>(attachment);
            const int coordinateCount = mesh->super.worldVerticesLength;
            worldVertices_.resize(static_cast<size_t>(coordinateCount));
            spVertexAttachment_computeWorldVertices(
                reinterpret_cast<spVertexAttachment*>(mesh), slot, 0, coordinateCount,
                worldVertices_.data(), 0, 2);
            positions = worldVertices_.data();
            uvs = mesh->uvs;
            indices = mesh->triangles;
            vertexCount = coordinateCount / 2;
            indexCount = mesh->trianglesCount;
            attachmentColor = &mesh->color;
            auto* atlasRegion = reinterpret_cast<spAtlasRegion*>(mesh->rendererObject);
            if (atlasRegion && atlasRegion->page)
                texture = static_cast<unsigned int>(reinterpret_cast<size_t>(atlasRegion->page->rendererObject));
        } else {
            spSkeletonClipping_clipEnd(clipper_, slot);
            continue;
        }

        if (!positions || !uvs || !indices || !attachmentColor || texture == 0 ||
            attachmentColor->a <= 0.0f) {
            spSkeletonClipping_clipEnd(clipper_, slot);
            continue;
        }

        if (spSkeletonClipping_isClipping(clipper_)) {
            spSkeletonClipping_clipTriangles(
                clipper_, positions, vertexCount * 2, indices, indexCount, uvs, 2);
            positions = clipper_->clippedVertices->items;
            uvs = clipper_->clippedUVs->items;
            indices = clipper_->clippedTriangles->items;
            vertexCount = clipper_->clippedVertices->size / 2;
            indexCount = clipper_->clippedTriangles->size;
        }

        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            const float x = positions[vertex * 2];
            const float y = positions[vertex * 2 + 1];
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

        const float alpha = skeleton->color.a * slot->color.a * attachmentColor->a;
        float lightR = skeleton->color.r * slot->color.r * attachmentColor->r;
        float lightG = skeleton->color.g * slot->color.g * attachmentColor->g;
        float lightB = skeleton->color.b * slot->color.b * attachmentColor->b;
        float darkR = slot->darkColor ? skeleton->color.r * slot->darkColor->r : 0.0f;
        float darkG = slot->darkColor ? skeleton->color.g * slot->darkColor->g : 0.0f;
        float darkB = slot->darkColor ? skeleton->color.b * slot->darkColor->b : 0.0f;
        if (premultipliedAlpha_) {
            lightR *= alpha;
            lightG *= alpha;
            lightB *= alpha;
            darkR *= alpha;
            darkG *= alpha;
            darkB *= alpha;
        }

        vertexData_.resize(static_cast<size_t>(vertexCount) * kFloatsPerVertex);
        for (int vertex = 0; vertex < vertexCount; ++vertex) {
            const int source = vertex * 2;
            const int target = vertex * kFloatsPerVertex;
            vertexData_[target] = positions[source];
            vertexData_[target + 1] = positions[source + 1];
            vertexData_[target + 2] = uvs[source];
            vertexData_[target + 3] = uvs[source + 1];
            vertexData_[target + 4] = lightR;
            vertexData_[target + 5] = lightG;
            vertexData_[target + 6] = lightB;
            vertexData_[target + 7] = alpha;
            vertexData_[target + 8] = darkR;
            vertexData_[target + 9] = darkG;
            vertexData_[target + 10] = darkB;
            vertexData_[target + 11] = premultipliedAlpha_ ? 1.0f : 0.0f;
        }

        applyBlendMode(slot->data->blendMode);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(vertexData_.size() * sizeof(float)),
            vertexData_.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(indexCount * sizeof(unsigned short)),
            indices, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);

        spSkeletonClipping_clipEnd(clipper_, slot);
    }

    spSkeletonClipping_clipEnd2(clipper_);
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
