#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <dwmapi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <climits>
#include <cstdlib>

#include "stb_image.h"
#include "AppSettings.h"
#include "AppState.h"
#include "Live2DModel.h"
#include "SpineModel.h"
#include "SpineRenderer.h"
#include "ControlPanel.h"
#include "ModelRegistry.h"
#include "PathUtils.h"

#pragma comment(lib, "dwmapi.lib")

// ---------- shaders ----------
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, nullptr, info);
        std::cerr << "Shader compilation error:\n" << info << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createShaderProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(prog, 512, nullptr, info);
        std::cerr << "Program linking error:\n" << info << std::endl;
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

std::string readFile(const char* path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return "";
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// orthographic projection matrix: maps (l,r,b,t) to (-1,-1)-(1,1)
void ortho(float* m, float l, float r, float b, float t, float n, float f) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2 / (r - l); m[5] = 2 / (t - b); m[10] = -2 / (f - n);
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n); m[15] = 1;
}

// ---------- main ----------
int main() {
    // --- window init (borderless fullscreen) ---
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW." << std::endl;
        return -1;
    }
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int scrW = mode->width;
    int scrH = mode->height;

    GLFWwindow* win = glfwCreateWindow(scrW, scrH, "Spine Pet", NULL, NULL);
    if (!win) {
        const char* description = nullptr;
        const int code = glfwGetError(&description);
        std::cerr << "Failed to create OpenGL window (" << code << "): "
                  << (description ? description : "unknown error") << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwSetWindowPos(win, 0, 0);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    HWND hwnd = glfwGetWin32Window(win);

    //背景透明
    MARGINS m = { -1,-1,-1,-1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);

    //点击穿透
    glfwSetWindowAttrib(win, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to load OpenGL functions." << std::endl;
        glfwDestroyWindow(win);
        glfwTerminate();
        return -1;
    }

    // --- load shaders ---
    const std::string assetsDir = assetRoot();
    std::string vs = readFile((assetsDir + "\\shaders\\sprite.vert").c_str());
    std::string fs = readFile((assetsDir + "\\shaders\\sprite.frag").c_str());
    GLuint shader = createShaderProgram(vs.c_str(), fs.c_str());
    SpineRenderer spineRenderer;
    if (!shader || !spineRenderer.initialize(shader)) {
        std::cerr << "Failed to initialize Spine renderer." << std::endl;
        glfwDestroyWindow(win);
        glfwTerminate();
        return -1;
    }
    const bool live2dFrameworkReady = Live2DModel::initializeFramework();
    if (!live2dFrameworkReady)
        std::cerr << "Live2D support could not be initialized; Spine remains available." << std::endl;

    // --- app state and spine model ---
    AppState state;
    const std::string kModelRegistryPath = assetsDir + "\\spine\\models.txt";
    if (!loadModelRegistry(kModelRegistryPath, state.models)) {
        state.models.clear();
        saveModelRegistry(kModelRegistryPath, state.models);
    }
    loadAppSettings(state);

    SpineModel spine;
    Live2DModel live2d;
    state.model = &spine;
    state.live2dModel = &live2d;
    state.renderWidth = static_cast<unsigned int>(scrW);
    state.renderHeight = static_cast<unsigned int>(scrH);
    if (!state.models.empty()) {
        if (state.currentModelIndex < 0 ||
            state.currentModelIndex >= static_cast<int>(state.models.size())) {
            state.currentModelIndex = 0;
        }
        const ModelConfig& first = state.models[state.currentModelIndex];
        std::cout << "Loading "
                  << (first.type == PetModelType::Live2D ? "Live2D" : "Spine")
                  << " model: " << first.name << std::endl;
        const bool firstLoaded = first.type == PetModelType::Live2D
            ? live2d.load(first.skeletonPath.c_str(), state.renderWidth, state.renderHeight)
            : spine.load(first.atlasPath.c_str(), first.skeletonPath.c_str());
        if (firstLoaded) {
            state.currentModelType = first.type;
            state.premultipliedAlpha = first.premultipliedAlpha;
            state.animations = first.type == PetModelType::Live2D
                ? live2d.getAnimationNames() : spine.getAnimationNames();
            state.skins = first.type == PetModelType::Live2D
                ? live2d.getExpressionNames() : spine.getSkinNames();
            if (!state.skins.empty()) {
                if (std::find(state.skins.begin(), state.skins.end(), state.currentSkin) ==
                    state.skins.end()) {
                    state.currentSkin = state.skins[0];
                }
                if (first.type == PetModelType::Live2D)
                    live2d.setExpression(state.currentSkin.c_str());
                else
                    spine.setSkin(state.currentSkin.c_str());
            }
            if (first.type == PetModelType::Live2D) {
                live2d.setTimeScale(state.animationSpeed);
            } else {
                spine.setDefaultMix(state.animationMix);
                spine.setTimeScale(state.animationSpeed);
                spine.setEventCallback([](const SpineEventInfo& event) {
                    std::cout << "Spine event: " << event.name << std::endl;
                });
            }
            if (!state.animations.empty()) {
                if (std::find(state.animations.begin(), state.animations.end(),
                        state.currentAnimation) == state.animations.end()) {
                    state.currentAnimation = state.animations[0];
                }
                if (first.type == PetModelType::Live2D)
                    live2d.setAnimation(state.currentAnimation.c_str(), state.animationLoop);
                else
                    spine.setAnimation(state.currentAnimation.c_str(), state.animationLoop);
            }
            if (first.type == PetModelType::Spine) {
                std::vector<std::string> restoredOverlays;
                for (const std::string& name : state.overlayAnimations) {
                    if (restoredOverlays.size() >= 31 ||
                        std::find(state.animations.begin(), state.animations.end(), name) ==
                            state.animations.end() ||
                        std::find(restoredOverlays.begin(), restoredOverlays.end(), name) !=
                            restoredOverlays.end()) {
                        continue;
                    }
                    const int trackIndex = static_cast<int>(restoredOverlays.size()) + 1;
                    if (spine.setAnimation(name.c_str(), state.animationLoop, trackIndex))
                        restoredOverlays.push_back(name);
                }
                state.overlayAnimations.swap(restoredOverlays);
            } else {
                state.overlayAnimations.clear();
            }
            std::cout << (first.type == PetModelType::Live2D ? "Live2D" : "Spine")
                      << " model loaded: " << first.name << std::endl;
        }
        else {
            std::cerr << "Failed to load pet model: " << first.name << std::endl;
            const std::string& loadError = first.type == PetModelType::Live2D
                ? live2d.lastError() : spine.lastError();
            if (!loadError.empty())
                std::cerr << (first.type == PetModelType::Live2D
                    ? "Live2D" : "Spine") << " load error: "
                          << loadError << std::endl;
            state.currentModelIndex = -1;
        }
    }

    // fallback test texture
    GLuint testTex = 0;
    GLuint testVAO = 0, testVBO = 0, testEBO = 0;
    float quadVerts[] = {
        -0.5f,  0.5f,   0.0f, 1.0f,
         0.5f,  0.5f,   1.0f, 1.0f,
        -0.5f, -0.5f,   0.0f, 0.0f,
         0.5f, -0.5f,   1.0f, 0.0f
    };
    unsigned int quadIdx[] = { 0,1,2, 1,3,2 };
    glGenVertexArrays(1, &testVAO);
    glGenBuffers(1, &testVBO);
    glGenBuffers(1, &testEBO);
    glBindVertexArray(testVAO);
    glBindBuffer(GL_ARRAY_BUFFER, testVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, testEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIdx), quadIdx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    int w, h, c;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* img = stbi_load((assetsDir + "\\test.png").c_str(), &w, &h, &c, 4);
    if (img) {
        glGenTextures(1, &testTex);
        glBindTexture(GL_TEXTURE_2D, testTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(img);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // control panel
    ControlPanel panel;
    if (state.model) {
        if (panel.create(GetModuleHandle(nullptr), &state)) {
            SetWindowPos(panel.hwnd(), HWND_TOPMOST,
                scrW - 320, 20, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE);
            std::cout << "Control panel created." << std::endl;
        }
        else {
            std::cerr << "Failed to create control panel." << std::endl;
        }
    }

    // --- projection: match screen pixel dimensions (center at origin) ---
    float modelScale = 0.3f; // model scale; larger means bigger model
    float proj[16] = {};
    int framebufferW = 0;
    int framebufferH = 0;

    // --- main loop ---
    double lastTime = glfwGetTime();
    bool mousePassthrough = true;
    bool petDragging = false;
    bool previousLeftButtonDown = false;
    POINT previousDragCursor = {};

    while (!glfwWindowShouldClose(win) && !state.quit) {
        if (state.alwaysOnTopChanged) {
            const HWND zOrder = state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
            if (!SetWindowPos(hwnd, zOrder, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
                std::cerr << "Failed to update pet always-on-top state, error: "
                          << GetLastError() << std::endl;
            }
            if (panel.hwnd()) {
                SetWindowPos(panel.hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            }
            state.alwaysOnTopChanged = false;
        }

        double now = glfwGetTime();
        float dt = std::min(static_cast<float>(now - lastTime), 0.1f);
        lastTime = now;
        int newFramebufferW = 0;
        int newFramebufferH = 0;
        glfwGetFramebufferSize(win, &newFramebufferW, &newFramebufferH);
        if ((newFramebufferW != framebufferW || newFramebufferH != framebufferH) &&
            newFramebufferW > 0 && newFramebufferH > 0) {
            framebufferW = newFramebufferW;
            framebufferH = newFramebufferH;
            state.renderWidth = static_cast<unsigned int>(framebufferW);
            state.renderHeight = static_cast<unsigned int>(framebufferH);
            glViewport(0, 0, framebufferW, framebufferH);
            ortho(proj,
                -(float)framebufferW / 2 / modelScale,
                (float)framebufferW / 2 / modelScale,
                -(float)framebufferH / 2 / modelScale,
                (float)framebufferH / 2 / modelScale,
                -1, 1);
        }

        POINT cursorScreen = {};
        const bool hasCursor = GetCursorPos(&cursorScreen) != FALSE;
        POINT cursorClient = cursorScreen;
        if (hasCursor) ScreenToClient(hwnd, &cursorClient);
        RECT clientRect = {};
        GetClientRect(hwnd, &clientRect);
        const float clientWidth = static_cast<float>(clientRect.right - clientRect.left);
        const float clientHeight = static_cast<float>(clientRect.bottom - clientRect.top);
        const bool leftButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        const bool live2dActive = state.currentModelType == PetModelType::Live2D;
        const bool hasModelBounds = live2dActive
            ? live2d.getNdcBounds(minX, minY, maxX, maxY)
            : spineRenderer.getBounds(minX, minY, maxX, maxY);
        const bool validViewport = framebufferW > 0 && framebufferH > 0 &&
            clientWidth > 0.0f && clientHeight > 0.0f;
        const float pixelsPerWorldX = validViewport
            ? (live2dActive ? clientWidth * 0.5f
                : modelScale * clientWidth / static_cast<float>(framebufferW))
            : 0.0f;
        const float pixelsPerWorldY = validViewport
            ? (live2dActive ? clientHeight * 0.5f
                : modelScale * clientHeight / static_cast<float>(framebufferH))
            : 0.0f;
        constexpr float kDragHitPadding = 8.0f;
        bool cursorOverPet = false;
        if (state.mouseDragEnabled && hasCursor && hasModelBounds && validViewport) {
            const float left = clientWidth * 0.5f + minX * pixelsPerWorldX - kDragHitPadding;
            const float right = clientWidth * 0.5f + maxX * pixelsPerWorldX + kDragHitPadding;
            const float top = clientHeight * 0.5f - maxY * pixelsPerWorldY - kDragHitPadding;
            const float bottom = clientHeight * 0.5f - minY * pixelsPerWorldY + kDragHitPadding;
            cursorOverPet = cursorClient.x >= left && cursorClient.x <= right &&
                cursorClient.y >= top && cursorClient.y <= bottom;

            const HWND cursorWindow = WindowFromPoint(cursorScreen);
            if (panel.hwnd() &&
                (cursorWindow == panel.hwnd() || IsChild(panel.hwnd(), cursorWindow))) {
                cursorOverPet = false;
            }
        }

        if (!state.mouseDragEnabled && petDragging) {
            petDragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
        } else if (state.mouseDragEnabled && cursorOverPet && leftButtonDown &&
                   !previousLeftButtonDown) {
            petDragging = true;
            previousDragCursor = cursorScreen;
            SetCapture(hwnd);
        }

        if (petDragging) {
            if (leftButtonDown && pixelsPerWorldX > 0.0f && pixelsPerWorldY > 0.0f) {
                const LONG deltaX = cursorScreen.x - previousDragCursor.x;
                const LONG deltaY = cursorScreen.y - previousDragCursor.y;
                if (live2dActive) {
                    state.positionX += static_cast<float>(deltaX);
                    state.positionY -= static_cast<float>(deltaY);
                } else {
                    state.positionX += static_cast<float>(deltaX) / pixelsPerWorldX;
                    state.positionY -= static_cast<float>(deltaY) / pixelsPerWorldY;
                }
                previousDragCursor = cursorScreen;
            } else if (!leftButtonDown) {
                petDragging = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                panel.refreshTransformControls();
                saveAppSettings(state);
            }
        }

        const bool shouldPassThrough =
            !state.mouseDragEnabled || (!cursorOverPet && !petDragging);
        if (shouldPassThrough != mousePassthrough) {
            glfwSetWindowAttrib(win, GLFW_MOUSE_PASSTHROUGH,
                shouldPassThrough ? GLFW_TRUE : GLFW_FALSE);
            mousePassthrough = shouldPassThrough;
        }
        previousLeftButtonDown = leftButtonDown;

        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (state.currentModelType == PetModelType::Live2D && live2d.loaded()) {
            // Cubism's OpenGL renderer uses client-side vertex/index arrays.
            // A VAO left bound by the fallback or Spine renderer makes Cubism
            // interpret model pointers as offsets and can render the atlas as
            // a flat texture after startup. Always draw Live2D from the default
            // compatibility-profile vertex state.
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            live2d.update(dt);
            live2d.draw(state.renderWidth, state.renderHeight,
                state.positionX, state.positionY, state.scale);
        }
        else if (state.currentModelType == PetModelType::Spine && spine.loaded()) {
            spine.setPosition(state.positionX, state.positionY);
            spine.setScale(state.scale);
            spine.update(dt);
            spine.applyAndUpdateWorldTransform();
            spineRenderer.setPremultipliedAlpha(state.premultipliedAlpha);
            if (spine.buildRenderData(state.premultipliedAlpha))
                spineRenderer.draw(spine.renderData(), proj);
        }
        else if (testTex) {
            glUseProgram(shader);
            glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, proj);
            glBindVertexArray(testVAO);
            glVertexAttrib4f(2, 1.0f, 1.0f, 1.0f, 1.0f);
            glVertexAttrib4f(3, 0.0f, 0.0f, 0.0f, 0.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, testTex);
            glUniform1i(glGetUniformLocation(shader, "image"), 0);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // cleanup
    saveAppSettings(state);
    panel.destroy();
    live2d.unload();
    spine.unload();
    if (testTex) glDeleteTextures(1, &testTex);
    if (testEBO) glDeleteBuffers(1, &testEBO);
    if (testVBO) glDeleteBuffers(1, &testVBO);
    if (testVAO) glDeleteVertexArrays(1, &testVAO);
    spineRenderer.shutdown();
    if (shader) glDeleteProgram(shader);
    Live2DModel::shutdownFramework();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
