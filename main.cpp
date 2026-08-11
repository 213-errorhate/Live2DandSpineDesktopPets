#define _CRT_SECURE_NO_WARNINGS
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

#include "stb_image.h"
#include "AppState.h"
#include "SpineModel.h"
#include "ControlPanel.h"
#include "ModelRegistry.h"
#include "PathUtils.h"

static std::wstring toWidePath(const char* path) {
    if (!path) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &result[0], len);
    if (!result.empty()) result.pop_back();
    return result;
}

extern "C" {
#include <spine/spine.h>

void _spAtlasPage_createTexture(spAtlasPage* self, const char* path) {
    std::wstring widePath = toWidePath(path);
    FILE* file = _wfopen(widePath.c_str(), L"rb");
    if (!file) return;
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::vector<unsigned char> data(len > 0 ? len : 0);
    if (len > 0) fread(data.data(), 1, len, file);
    fclose(file);

    int w, h, c;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* img = stbi_load_from_memory(data.data(), (int)data.size(), &w, &h, &c, 4);
    if (img) {
        GLuint tid;
        glGenTextures(1, &tid);
        glBindTexture(GL_TEXTURE_2D, tid);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        stbi_image_free(img);
        self->rendererObject = (void*)(size_t)tid;
    }
}

void _spAtlasPage_disposeTexture(spAtlasPage* self) {
    GLuint tid = (GLuint)(size_t)self->rendererObject;
    if (tid) glDeleteTextures(1, &tid);
}

char* _spUtil_readFile(const char* path, int* length) {
    std::wstring widePath = toWidePath(path);
    FILE* file = _wfopen(widePath.c_str(), L"rb");
    if (!file) return 0;
    fseek(file, 0, SEEK_END);
    *length = (int)ftell(file);
    fseek(file, 0, SEEK_SET);
    char* data = (char*)malloc(*length);
    fread(data, 1, *length, file);
    fclose(file);
    return data;
}

}

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
    }
    return shader;
}

GLuint createShaderProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
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

// ---------- global render objects ----------
GLuint vao = 0, vbo = 0, ebo = 0, shader = 0;

void initGL() {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// ---------- draw Spine skeleton ----------
void drawSkeleton(spSkeleton* skel, float* proj) {
    if (!skel) return;
    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, proj);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // Spine premultiplied alpha

    for (int i = 0; i < skel->slotsCount; ++i) {
        spSlot* slot = skel->drawOrder[i];
        if (!slot || !slot->attachment) continue;

        float* vertices = nullptr; int vcount = 0;
        unsigned short* indices = nullptr; int icount = 0;
        float* uvs = nullptr; GLuint tex = 0;
        float regionVerts[8];

        if (slot->attachment->type == SP_ATTACHMENT_REGION) {
            spRegionAttachment* reg = (spRegionAttachment*)slot->attachment;
            spRegionAttachment_computeWorldVertices(reg, slot->bone, regionVerts, 0, 2);
            vertices = regionVerts; vcount = 4;
            uvs = reg->uvs;
            spAtlasRegion* atlasReg = (spAtlasRegion*)reg->rendererObject;
            tex = atlasReg ? (GLuint)(size_t)atlasReg->page->rendererObject : 0;
            static unsigned short quad[] = { 0,1,2,2,3,0 };
            indices = quad; icount = 6;
        }
        else if (slot->attachment->type == SP_ATTACHMENT_MESH) {
            spMeshAttachment* mesh = (spMeshAttachment*)slot->attachment;
            int coordCount = mesh->super.worldVerticesLength;
            float* verts = new float[coordCount];
            spVertexAttachment_computeWorldVertices(
                (spVertexAttachment*)mesh, slot,
                0, mesh->super.worldVerticesLength,
                verts, 0, 2);
            vertices = verts; vcount = coordCount / 2;
            uvs = mesh->uvs;
            indices = mesh->triangles; icount = mesh->trianglesCount;
            spAtlasRegion* meshReg = (spAtlasRegion*)mesh->rendererObject;
            tex = meshReg ? (GLuint)(size_t)meshReg->page->rendererObject : 0;
        }
        else continue;

        if (!vertices || !uvs || tex == 0) {
            if (slot->attachment->type == SP_ATTACHMENT_MESH) delete[] vertices;
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, tex);
        std::vector<float> buf(vcount * 4);
        for (int j = 0; j < vcount; ++j) {
            buf[j * 4] = vertices[j * 2]; buf[j * 4 + 1] = vertices[j * 2 + 1];
            buf[j * 4 + 2] = uvs[j * 2]; buf[j * 4 + 3] = uvs[j * 2 + 1];
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, icount * sizeof(unsigned short), indices, GL_DYNAMIC_DRAW);
        glDrawElements(GL_TRIANGLES, icount, GL_UNSIGNED_SHORT, 0);

        if (slot->attachment->type == SP_ATTACHMENT_MESH) delete[] vertices;
    }
    glBindVertexArray(0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

// ---------- main ----------
int main() {
    // --- window init (borderless fullscreen) ---
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    int scrW = mode->width;
    int scrH = mode->height;

    GLFWwindow* win = glfwCreateWindow(scrW, scrH, "Spine Pet", NULL, NULL);
    if (!win) { glfwTerminate(); return -1; }
    glfwSetWindowPos(win, 0, 0);
    glfwMakeContextCurrent(win);

    HWND hwnd = glfwGetWin32Window(win);

    //背景透明
    MARGINS m = { -1,-1,-1,-1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);

    //点击穿透
    glfwSetWindowAttrib(win, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);

    // Window style: keep normal window while the control panel is being developed.
    // TODO: re-enable after testing the panel.
    // glfwSetWindowAttrib(win, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
    // SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(win);
        glfwTerminate();
        return -1;
    }

    // --- load shaders ---
    const std::string assetsDir = assetRoot();
    std::string vs = readFile((assetsDir + "\\shaders\\sprite.vert").c_str());
    std::string fs = readFile((assetsDir + "\\shaders\\sprite.frag").c_str());
    shader = createShaderProgram(vs.c_str(), fs.c_str());
    initGL();

    // --- app state and spine model ---
    AppState state;
    const std::string kModelRegistryPath = assetsDir + "\\spine\\models.txt";
    if (!loadModelRegistry(kModelRegistryPath, state.models)) {
        state.models = {
            { "idle_8",
              assetsDir + "\\spine\\idle_8\\Spine_Idle_8.atlas",
              assetsDir + "\\spine\\idle_8\\Spine_Idle_8.skel" },
        };
        saveModelRegistry(kModelRegistryPath, state.models);
    }

    SpineModel spine;
    state.model = &spine;
    if (!state.models.empty()) {
        state.currentModelIndex = 0;
        const ModelConfig& first = state.models[0];
        if (spine.load(first.atlasPath.c_str(), first.skeletonPath.c_str())) {
            state.animations = spine.getAnimationNames();
            if (!state.animations.empty()) {
                state.currentAnimation = state.animations[0];
                spine.setAnimation(state.currentAnimation.c_str(), true);
            }
            std::cout << "Spine model loaded: " << first.name << std::endl;
        }
        else {
            std::cerr << "Failed to load spine model: " << first.name << std::endl;
        }
    }

    // fallback test texture
    GLuint testTex = 0;
    GLuint testVAO = 0, testVBO = 0, testEBO = 0;
    if (!spine.loaded()) {
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
        unsigned char* img = stbi_load("assets/test.png", &w, &h, &c, 4);
        if (img) {
            glGenTextures(1, &testTex);
            glBindTexture(GL_TEXTURE_2D, testTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            stbi_image_free(img);
        }
    }

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
    float proj[16];
    ortho(proj,
        -(float)scrW / 2 / modelScale,
        (float)scrW / 2 / modelScale,
        -(float)scrH / 2 / modelScale,
        (float)scrH / 2 / modelScale,
        -1, 1);

    // --- main loop ---
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(win) && !state.quit) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        if (spine.loaded()) {
            spine.setPosition(state.positionX, state.positionY);
            spine.setScale(state.scale);
            spine.update(dt);
            spine.applyAndUpdateWorldTransform();
            drawSkeleton(spine.skeleton(), proj);
        }
        else if (testTex) {
            glUseProgram(shader);
            glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, proj);
            glBindVertexArray(testVAO);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, testTex);
            glUniform1i(glGetUniformLocation(shader, "image"), 0);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // cleanup
    panel.destroy();
    spine.unload();
    if (testTex) glDeleteTextures(1, &testTex);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
