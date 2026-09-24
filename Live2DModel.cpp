#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <GL/glew.h>

#include "Live2DModel.h"
#include "PathUtils.h"
#include "stb_image.h"

#include <CubismDefaultParameterId.hpp>
#include <CubismFramework.hpp>
#include <CubismModelSettingJson.hpp>
#include <Effect/CubismBreath.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <ICubismAllocator.hpp>
#include <Id/CubismIdManager.hpp>
#include <Math/CubismMatrix44.hpp>
#include <Model/CubismModel.hpp>
#include <Model/CubismUserModel.hpp>
#include <Motion/ACubismMotion.hpp>
#include <Motion/CubismBreathUpdater.hpp>
#include <Motion/CubismExpressionUpdater.hpp>
#include <Motion/CubismEyeBlinkUpdater.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismPhysicsUpdater.hpp>
#include <Motion/CubismPoseUpdater.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Rendering/OpenGL/CubismOffscreenManager_OpenGLES2.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Utils/CubismString.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <utility>

namespace Csm = Live2D::Cubism::Framework;

namespace {

constexpr Csm::csmInt32 kMotionPriorityForce = 3;
bool gFrameworkReady = false;
std::string gFrameworkShaderDirectory;
Csm::CubismFramework::Option gFrameworkOption;

std::string directoryName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string fileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool readFileBytes(const std::string& path, std::vector<unsigned char>& bytes) {
    bytes.clear();
    FILE* file = _wfopen(toWideUtf8(path).c_str(), L"rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    const long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(length));
    const size_t read = fread(bytes.data(), 1, bytes.size(), file);
    fclose(file);
    if (read != bytes.size()) {
        bytes.clear();
        return false;
    }
    return true;
}

class CubismAllocator final : public Csm::ICubismAllocator {
public:
    void* Allocate(const Csm::csmSizeType size) override {
        return std::malloc(size);
    }

    void Deallocate(void* memory) override {
        std::free(memory);
    }

    void* AllocateAligned(const Csm::csmSizeType size,
                          const Csm::csmUint32 alignment) override {
        return _aligned_malloc(size, alignment);
    }

    void DeallocateAligned(void* alignedMemory) override {
        _aligned_free(alignedMemory);
    }
};

CubismAllocator gAllocator;

void cubismLog(const char* message) {
    if (message) std::cerr << "[Live2D] " << message << std::endl;
}

Csm::csmByte* cubismLoadFile(const std::string path, Csm::csmSizeInt* size) {
    if (!size) return nullptr;
    *size = 0;
    if (path.empty()) return nullptr;

    std::string resolved(path);
    if (resolved.rfind("FrameworkShaders/", 0) == 0 ||
        resolved.rfind("FrameworkShaders\\", 0) == 0) {
        resolved = gFrameworkShaderDirectory + fileName(resolved);
    }

    std::vector<unsigned char> bytes;
    if (!readFileBytes(resolved, bytes)) return nullptr;
    auto* result = static_cast<Csm::csmByte*>(std::malloc(bytes.size()));
    if (!result) return nullptr;
    std::memcpy(result, bytes.data(), bytes.size());
    *size = static_cast<Csm::csmSizeInt>(bytes.size());
    return result;
}

void cubismReleaseFile(Csm::csmByte* bytes) {
    std::free(bytes);
}

int countUniqueMaskGroups(Csm::csmInt32 objectCount,
                          const Csm::csmInt32** objectMasks,
                          const Csm::csmInt32* objectMaskCounts) {
    if (objectCount <= 0 || !objectMasks || !objectMaskCounts) return 0;

    std::vector<std::vector<Csm::csmInt32>> groups;
    for (Csm::csmInt32 object = 0; object < objectCount; ++object) {
        const Csm::csmInt32 count = objectMaskCounts[object];
        if (count <= 0 || !objectMasks[object]) continue;

        std::vector<Csm::csmInt32> group(
            objectMasks[object], objectMasks[object] + count);
        std::sort(group.begin(), group.end());
        group.erase(std::unique(group.begin(), group.end()), group.end());
        if (std::find(groups.begin(), groups.end(), group) == groups.end())
            groups.push_back(std::move(group));
    }
    return static_cast<int>(groups.size());
}

int selectMaskRenderTextureCount(const Csm::CubismModel* model,
                                 int& uniqueMaskGroupCount) {
    uniqueMaskGroupCount = 0;
    if (!model) return 1;

    const int drawableGroups = countUniqueMaskGroups(
        model->GetDrawableCount(), model->GetDrawableMasks(),
        model->GetDrawableMaskCounts());
    const int offscreenGroups = countUniqueMaskGroups(
        model->GetOffscreenCount(), model->GetOffscreenMasks(),
        model->GetOffscreenMaskCounts());
    uniqueMaskGroupCount = std::max(drawableGroups, offscreenGroups);

    // Cubism packs up to 36 mask groups into one render texture. With two or
    // more textures it uses up to 32 groups per texture.
    if (uniqueMaskGroupCount <= 36) return 1;
    return std::max(2, (uniqueMaskGroupCount + 31) / 32);
}

} // namespace

class Live2DModel::Impl final : public Csm::CubismUserModel {
public:
    Impl() = default;

    ~Impl() override {
        DeleteRenderer();
        for (unsigned int texture : textures_) {
            if (texture) glDeleteTextures(1, &texture);
        }
        for (auto& entry : motions_) Csm::ACubismMotion::Delete(entry.second);
        for (auto& entry : expressions_) Csm::ACubismMotion::Delete(entry.second);
        delete modelSetting_;
    }

    bool load(const std::string& modelJsonPath, unsigned int renderWidth,
              unsigned int renderHeight, std::string& error) {
        modelHomeDirectory_ = directoryName(modelJsonPath);

        std::vector<unsigned char> bytes;
        if (!readFileBytes(modelJsonPath, bytes)) {
            error = "Failed to read Live2D model settings: " + modelJsonPath;
            return false;
        }
        modelSetting_ = new Csm::CubismModelSettingJson(
            reinterpret_cast<Csm::csmByte*>(bytes.data()),
            static_cast<Csm::csmSizeInt>(bytes.size()));
        if (!modelSetting_) {
            error = "Failed to parse Live2D model settings.";
            return false;
        }

        const char* mocName = modelSetting_->GetModelFileName();
        if (!mocName || !*mocName || !loadAsset(mocName, bytes)) {
            error = "Live2D model3.json does not reference a readable moc3 file.";
            return false;
        }
        LoadModel(reinterpret_cast<Csm::csmByte*>(bytes.data()),
                  static_cast<Csm::csmSizeInt>(bytes.size()), true);
        if (!_model || !_modelMatrix) {
            error = "Cubism Core rejected the moc3 model.";
            return false;
        }

        Csm::csmMap<Csm::csmString, Csm::csmFloat32> layout;
        modelSetting_->GetLayoutMap(layout);
        _modelMatrix->SetupFromLayout(layout);

        for (Csm::csmInt32 i = 0; i < modelSetting_->GetEyeBlinkParameterCount(); ++i)
            eyeBlinkIds_.PushBack(modelSetting_->GetEyeBlinkParameterId(i));
        for (Csm::csmInt32 i = 0; i < modelSetting_->GetLipSyncParameterCount(); ++i)
            lipSyncIds_.PushBack(modelSetting_->GetLipSyncParameterId(i));

        loadExpressions();
        loadEffects();
        loadMotions();
        _updateScheduler.SortUpdatableList();
        _model->SaveParameters();
        _motionManager->StopAllMotions();

        renderWidth_ = std::max(1u, renderWidth);
        renderHeight_ = std::max(1u, renderHeight);
        int uniqueMaskGroupCount = 0;
        const int maskRenderTextureCount =
            selectMaskRenderTextureCount(_model, uniqueMaskGroupCount);
        CreateRenderer(renderWidth_, renderHeight_, maskRenderTextureCount);
        if (uniqueMaskGroupCount > 0) {
            std::cout << "Live2D mask groups: " << uniqueMaskGroupCount
                      << ", render textures: " << maskRenderTextureCount << std::endl;
        }
        if (!GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>()) {
            error = "Failed to create the Live2D OpenGL renderer.";
            return false;
        }
        if (!setupTextures(error)) return false;

        _updating = false;
        _initialized = true;

        auto idle = std::find_if(animationNames_.begin(), animationNames_.end(),
            [](const std::string& value) { return value.rfind("Idle/", 0) == 0; });
        if (idle != animationNames_.end()) setAnimation(idle->c_str(), true);
        else if (!animationNames_.empty()) setAnimation(animationNames_.front().c_str(), true);
        return true;
    }

    void update(float deltaTime) {
        if (!_model) return;
        const float scaledDelta = std::max(0.0f, deltaTime) * timeScale_;
        motionUpdated_ = false;
        _model->LoadParameters();
        if (!_motionManager->IsFinished())
            motionUpdated_ = _motionManager->UpdateMotion(_model, scaledDelta);
        _model->SaveParameters();
        _updateScheduler.OnLateUpdate(_model, scaledDelta);
        _model->Update();
    }

    void draw(unsigned int renderWidth, unsigned int renderHeight,
              float positionX, float positionY, float scale) {
        if (!_model || !_modelMatrix) return;
        renderWidth = std::max(1u, renderWidth);
        renderHeight = std::max(1u, renderHeight);
        if (renderWidth != renderWidth_ || renderHeight != renderHeight_) {
            renderWidth_ = renderWidth;
            renderHeight_ = renderHeight;
            SetRenderTargetSize(renderWidth_, renderHeight_);
        }

        Csm::CubismMatrix44 matrix;
        matrix.LoadIdentity();
        // Keep the pet position in screen pixels.  Cubism's relative transforms
        // are post-multiplied, so translation must be added before model scaling;
        // otherwise dragging is multiplied by the model scale/aspect ratio and
        // the grabbed point cannot stay under the cursor.
        matrix.TranslateRelative(
            positionX * 2.0f / static_cast<float>(renderWidth_),
            positionY * 2.0f / static_cast<float>(renderHeight_));
        matrix.ScaleRelative(
            static_cast<float>(renderHeight_) / static_cast<float>(renderWidth_), 1.0f);
        const float safeScale = std::clamp(scale, 0.05f, 5.0f);
        matrix.ScaleRelative(safeScale, safeScale);
        matrix.MultiplyByMatrix(_modelMatrix);
        updateBounds(matrix);

        auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
        if (!renderer) return;
        Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->BeginFrameProcess();
        renderer->SetMvpMatrix(&matrix);
        renderer->DrawModel();
        Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()->EndFrameProcess();
        Csm::Rendering::CubismOffscreenManager_OpenGLES2::GetInstance()
            ->ReleaseStaleRenderTextures();
    }

    bool setAnimation(const char* name, bool loop) {
        if (!name) return false;
        const auto found = motions_.find(name);
        if (found == motions_.end() || !found->second) return false;
        auto* motion = static_cast<Csm::CubismMotion*>(found->second);
        motion->SetLoop(loop);
        _motionManager->StopAllMotions();
        _motionManager->SetReservePriority(kMotionPriorityForce);
        if (_motionManager->StartMotionPriority(motion, false, kMotionPriorityForce) ==
            Csm::InvalidMotionQueueEntryHandleValue) return false;
        currentAnimation_ = name;
        animationLoop_ = loop;
        return true;
    }

    void setCurrentLoop(bool loop) {
        animationLoop_ = loop;
        const auto found = motions_.find(currentAnimation_);
        if (found != motions_.end() && found->second)
            static_cast<Csm::CubismMotion*>(found->second)->SetLoop(loop);
    }

    bool setExpression(const char* name) {
        if (!name) return false;
        const auto found = expressions_.find(name);
        if (found == expressions_.end() || !found->second) return false;
        _expressionManager->StartMotion(found->second, false);
        return true;
    }

    void setTimeScale(float scale) {
        timeScale_ = std::clamp(scale, 0.0f, 5.0f);
    }

    const std::vector<std::string>& animationNames() const { return animationNames_; }
    const std::vector<std::string>& expressionNames() const { return expressionNames_; }

    bool getNdcBounds(float& minX, float& minY, float& maxX, float& maxY) const {
        if (!boundsValid_) return false;
        minX = minX_;
        minY = minY_;
        maxX = maxX_;
        maxY = maxY_;
        return true;
    }

private:
    bool loadAsset(const std::string& relativePath, std::vector<unsigned char>& bytes) const {
        return readFileBytes(modelHomeDirectory_ + relativePath, bytes);
    }

    void loadExpressions() {
        std::vector<unsigned char> bytes;
        for (Csm::csmInt32 i = 0; i < modelSetting_->GetExpressionCount(); ++i) {
            const char* name = modelSetting_->GetExpressionName(i);
            const char* path = modelSetting_->GetExpressionFileName(i);
            if (!name || !*name || !path || !*path || !loadAsset(path, bytes)) continue;
            Csm::ACubismMotion* expression = LoadExpression(
                reinterpret_cast<Csm::csmByte*>(bytes.data()),
                static_cast<Csm::csmSizeInt>(bytes.size()), name);
            if (!expression) continue;
            expressions_[name] = expression;
            expressionNames_.emplace_back(name);
        }
        if (!expressions_.empty()) {
            _updateScheduler.AddUpdatableList(
                CSM_NEW Csm::CubismExpressionUpdater(*_expressionManager));
        }
    }

    void loadEffects() {
        std::vector<unsigned char> bytes;
        const char* posePath = modelSetting_->GetPoseFileName();
        if (posePath && *posePath && loadAsset(posePath, bytes)) {
            LoadPose(reinterpret_cast<Csm::csmByte*>(bytes.data()),
                     static_cast<Csm::csmSizeInt>(bytes.size()));
            if (_pose) _updateScheduler.AddUpdatableList(CSM_NEW Csm::CubismPoseUpdater(*_pose));
        }

        const char* physicsPath = modelSetting_->GetPhysicsFileName();
        if (physicsPath && *physicsPath && loadAsset(physicsPath, bytes)) {
            LoadPhysics(reinterpret_cast<Csm::csmByte*>(bytes.data()),
                        static_cast<Csm::csmSizeInt>(bytes.size()));
            if (_physics)
                _updateScheduler.AddUpdatableList(CSM_NEW Csm::CubismPhysicsUpdater(*_physics));
        }

        if (eyeBlinkIds_.GetSize() > 0) {
            _eyeBlink = Csm::CubismEyeBlink::Create(modelSetting_);
            if (_eyeBlink) {
                _updateScheduler.AddUpdatableList(
                    CSM_NEW Csm::CubismEyeBlinkUpdater(motionUpdated_, *_eyeBlink));
            }
        }

        _breath = Csm::CubismBreath::Create();
        if (_breath) {
            using namespace Csm::DefaultParameterId;
            Csm::csmVector<Csm::CubismBreath::BreathParameterData> parameters;
            auto* ids = Csm::CubismFramework::GetIdManager();
            parameters.PushBack(Csm::CubismBreath::BreathParameterData(
                ids->GetId(ParamAngleX), 0.0f, 15.0f, 6.5f, 0.5f));
            parameters.PushBack(Csm::CubismBreath::BreathParameterData(
                ids->GetId(ParamAngleY), 0.0f, 8.0f, 3.5f, 0.5f));
            parameters.PushBack(Csm::CubismBreath::BreathParameterData(
                ids->GetId(ParamAngleZ), 0.0f, 10.0f, 5.5f, 0.5f));
            parameters.PushBack(Csm::CubismBreath::BreathParameterData(
                ids->GetId(ParamBodyAngleX), 0.0f, 4.0f, 15.5f, 0.5f));
            parameters.PushBack(Csm::CubismBreath::BreathParameterData(
                ids->GetId(ParamBreath), 0.5f, 0.5f, 3.2f, 0.5f));
            _breath->SetParameters(parameters);
            _updateScheduler.AddUpdatableList(CSM_NEW Csm::CubismBreathUpdater(*_breath));
        }
    }

    void loadMotions() {
        std::vector<unsigned char> bytes;
        for (Csm::csmInt32 groupIndex = 0;
             groupIndex < modelSetting_->GetMotionGroupCount(); ++groupIndex) {
            const char* group = modelSetting_->GetMotionGroupName(groupIndex);
            if (!group || !*group) continue;
            const Csm::csmInt32 count = modelSetting_->GetMotionCount(group);
            for (Csm::csmInt32 index = 0; index < count; ++index) {
                const char* path = modelSetting_->GetMotionFileName(group, index);
                if (!path || !*path || !loadAsset(path, bytes)) continue;
                const std::string name = std::string(group) + "/" + std::to_string(index);
                auto* motion = static_cast<Csm::CubismMotion*>(LoadMotion(
                    reinterpret_cast<Csm::csmByte*>(bytes.data()),
                    static_cast<Csm::csmSizeInt>(bytes.size()), name.c_str(),
                    nullptr, nullptr, modelSetting_, group, index, true));
                if (!motion) continue;
                motion->SetEffectIds(eyeBlinkIds_, lipSyncIds_);
                motions_[name] = motion;
                animationNames_.push_back(name);
            }
        }
    }

    bool setupTextures(std::string& error) {
        auto* renderer = GetRenderer<Csm::Rendering::CubismRenderer_OpenGLES2>();
        if (!renderer) return false;
        GLint previousTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        for (Csm::csmInt32 index = 0; index < modelSetting_->GetTextureCount(); ++index) {
            const char* relativePath = modelSetting_->GetTextureFileName(index);
            std::vector<unsigned char> fileBytes;
            if (!relativePath || !*relativePath || !loadAsset(relativePath, fileBytes)) {
                error = "Failed to read Live2D texture: " +
                    std::string(relativePath ? relativePath : "unknown");
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
                return false;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load(0);
            unsigned char* pixels = stbi_load_from_memory(
                fileBytes.data(), static_cast<int>(fileBytes.size()),
                &width, &height, &channels, 4);
            if (!pixels) {
                error = "Failed to decode Live2D texture: " + std::string(relativePath);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
                return false;
            }

            GLuint texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            stbi_image_free(pixels);

            textures_.push_back(texture);
            renderer->BindTexture(index, texture);
        }
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        renderer->IsPremultipliedAlpha(false);
        return true;
    }

    void updateBounds(Csm::CubismMatrix44& matrix) {
        boundsValid_ = false;
        for (Csm::csmInt32 drawable = 0; drawable < _model->GetDrawableCount(); ++drawable) {
            if (!_model->GetDrawableDynamicFlagIsVisible(drawable) ||
                _model->GetDrawableOpacity(drawable) <= 0.0f) continue;
            const Csm::csmInt32 count = _model->GetDrawableVertexCount(drawable);
            const Csm::csmFloat32* vertices = _model->GetDrawableVertices(drawable);
            if (!vertices) continue;
            for (Csm::csmInt32 vertex = 0; vertex < count; ++vertex) {
                const float x = matrix.TransformX(vertices[vertex * 2]);
                const float y = matrix.TransformY(vertices[vertex * 2 + 1]);
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
        }
    }

    Csm::CubismModelSettingJson* modelSetting_ = nullptr;
    std::string modelHomeDirectory_;
    std::map<std::string, Csm::ACubismMotion*> motions_;
    std::map<std::string, Csm::ACubismMotion*> expressions_;
    std::vector<std::string> animationNames_;
    std::vector<std::string> expressionNames_;
    std::vector<unsigned int> textures_;
    Csm::csmVector<Csm::CubismIdHandle> eyeBlinkIds_;
    Csm::csmVector<Csm::CubismIdHandle> lipSyncIds_;
    std::string currentAnimation_;
    float timeScale_ = 1.0f;
    bool animationLoop_ = true;
    Csm::csmBool motionUpdated_ = false;
    unsigned int renderWidth_ = 1;
    unsigned int renderHeight_ = 1;
    bool boundsValid_ = false;
    float minX_ = 0.0f;
    float minY_ = 0.0f;
    float maxX_ = 0.0f;
    float maxY_ = 0.0f;
};

Live2DModel::Live2DModel() = default;
Live2DModel::~Live2DModel() = default;

bool Live2DModel::initializeFramework() {
    if (gFrameworkReady) return true;
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW for Live2D." << std::endl;
        return false;
    }
    glGetError();

    std::string assets = assetRoot();
    const size_t slash = assets.find_last_of("/\\");
    const std::string projectRoot = slash == std::string::npos ? "" : assets.substr(0, slash);
    gFrameworkShaderDirectory = projectRoot +
        "\\external\\CubismSdkForNative\\Framework\\src\\Rendering\\OpenGL\\Shaders\\Standard\\";

    gFrameworkOption.LogFunction = cubismLog;
    gFrameworkOption.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Warning;
    gFrameworkOption.LoadFileFunction = cubismLoadFile;
    gFrameworkOption.ReleaseBytesFunction = cubismReleaseFile;
    if (!Csm::CubismFramework::StartUp(&gAllocator, &gFrameworkOption)) return false;
    Csm::CubismFramework::Initialize();
    gFrameworkReady = Csm::CubismFramework::IsInitialized();
    return gFrameworkReady;
}

void Live2DModel::shutdownFramework() {
    if (!gFrameworkReady) return;
    Csm::Rendering::CubismOffscreenManager_OpenGLES2::ReleaseInstance();
    Csm::Rendering::CubismRenderer::StaticRelease();
    Csm::CubismFramework::Dispose();
    Csm::CubismFramework::CleanUp();
    gFrameworkReady = false;
}

bool Live2DModel::frameworkReady() {
    return gFrameworkReady;
}

bool Live2DModel::load(const char* modelJsonPath, unsigned int renderWidth,
                       unsigned int renderHeight) {
    lastError_.clear();
    if (!gFrameworkReady) {
        lastError_ = "Live2D Cubism Framework is not initialized.";
        return false;
    }
    if (!modelJsonPath || !*modelJsonPath) {
        lastError_ = "A Live2D model3.json path is required.";
        return false;
    }
    std::unique_ptr<Impl> candidate(new Impl());
    if (!candidate->load(modelJsonPath, renderWidth, renderHeight, lastError_)) return false;
    impl_ = std::move(candidate);
    return true;
}

void Live2DModel::unload() {
    impl_.reset();
}

bool Live2DModel::loaded() const {
    return impl_ != nullptr;
}

void Live2DModel::update(float deltaTime) {
    if (impl_) impl_->update(deltaTime);
}

void Live2DModel::draw(unsigned int renderWidth, unsigned int renderHeight,
                       float positionX, float positionY, float scale) {
    if (impl_) impl_->draw(renderWidth, renderHeight, positionX, positionY, scale);
}

bool Live2DModel::setAnimation(const char* name, bool loop) {
    return impl_ && impl_->setAnimation(name, loop);
}

void Live2DModel::setCurrentLoop(bool loop) {
    if (impl_) impl_->setCurrentLoop(loop);
}

void Live2DModel::setTimeScale(float scale) {
    if (impl_) impl_->setTimeScale(scale);
}

bool Live2DModel::setExpression(const char* name) {
    return impl_ && impl_->setExpression(name);
}

std::vector<std::string> Live2DModel::getAnimationNames() const {
    return impl_ ? impl_->animationNames() : std::vector<std::string>();
}

std::vector<std::string> Live2DModel::getExpressionNames() const {
    return impl_ ? impl_->expressionNames() : std::vector<std::string>();
}

bool Live2DModel::getNdcBounds(float& minX, float& minY, float& maxX, float& maxY) const {
    return impl_ && impl_->getNdcBounds(minX, minY, maxX, maxY);
}
