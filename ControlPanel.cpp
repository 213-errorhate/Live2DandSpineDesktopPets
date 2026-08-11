#include "ControlPanel.h"
#include "AppState.h"
#include "ModelRegistry.h"
#include "PathUtils.h"
#include "SpineModel.h"

#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

const wchar_t* kWindowClass = L"PetControlPanel";
const int kIdModel = 1000;
const int kIdList = 1001;
const int kIdApply = 1002;
const int kIdLoad = 1003;
const int kIdRemove = 1004;
const int kIdEditX = 1005;
const int kIdEditY = 1006;
const int kIdEditScale = 1007;
const int kIdApplyTransform = 1008;

std::wstring toWide(const std::string& text) {
    if (text.empty()) return L"";
    int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (length <= 0) return L"";
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &result[0], length);
    return result;
}

std::string toNarrow(const std::wstring& text) {
    if (text.empty()) return "";
    int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                     nullptr, 0, nullptr, nullptr);
    if (length <= 0) return "";
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                        &result[0], length, nullptr, nullptr);
    return result;
}

std::string uniqueModelName(const std::vector<ModelConfig>& models, const std::string& base) {
    bool exists = false;
    for (const auto& model : models) {
        if (model.name == base) {
            exists = true;
            break;
        }
    }
    if (!exists) return base;

    int suffix = 2;
    while (true) {
        std::string candidate = base + "_" + std::to_string(suffix);
        bool used = false;
        for (const auto& model : models) {
            if (model.name == candidate) {
                used = true;
                break;
            }
        }
        if (!used) return candidate;
        ++suffix;
    }
}

} // namespace

LRESULT CALLBACK ControlPanel::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ControlPanel* panel = reinterpret_cast<ControlPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ControlPanel* self = reinterpret_cast<ControlPanel*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        HINSTANCE instance = createStruct->hInstance;

        self->tabControl_ = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
            0, 0, 300, 28, hwnd,
            reinterpret_cast<HMENU>(1), instance, nullptr);

        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<LPWSTR>(L"加载模型");
        TabCtrl_InsertItem(self->tabControl_, 0, &item);
        item.pszText = const_cast<LPWSTR>(L"控制");
        TabCtrl_InsertItem(self->tabControl_, 1, &item);
        TabCtrl_SetCurSel(self->tabControl_, 0);

        // Page 1: model and animation
        self->modelLabel_ = CreateWindowExW(0, L"STATIC", L"Model:",
            WS_CHILD | WS_VISIBLE, 10, 35, 260, 20,
            hwnd, nullptr, instance, nullptr);

        self->modelCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            10, 60, 260, 140, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdModel)),
            instance, nullptr);

        self->animLabel_ = CreateWindowExW(0, L"STATIC", L"Animation list:",
            WS_CHILD | WS_VISIBLE, 10, 120, 260, 20,
            hwnd, nullptr, instance, nullptr);

        self->listBox_ = CreateWindowExW(0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            10, 145, 260, 160, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)),
            instance, nullptr);

        self->applyAnimButton_ = CreateWindowExW(0, L"BUTTON", L"Apply Anim",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 315, 80, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApply)),
            instance, nullptr);

        self->loadButton_ = CreateWindowExW(0, L"BUTTON", L"Load Model...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            95, 315, 95, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLoad)),
            instance, nullptr);

        self->removeButton_ = CreateWindowExW(0, L"BUTTON", L"Remove",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            195, 315, 85, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRemove)),
            instance, nullptr);

        self->statusText_ = CreateWindowExW(0, L"STATIC", L"Current: none",
            WS_CHILD | WS_VISIBLE, 10, 355, 260, 25,
            hwnd, nullptr, instance, nullptr);

        // Page 2: transform
        self->transformLabel_ = CreateWindowExW(0, L"STATIC", L"Transform:",
            WS_CHILD | WS_VISIBLE, 10, 35, 260, 20,
            hwnd, nullptr, instance, nullptr);

        self->xLabel_ = CreateWindowExW(0, L"STATIC", L"X:",
            WS_CHILD | WS_VISIBLE, 10, 65, 20, 22,
            hwnd, nullptr, instance, nullptr);
        self->editX_ = CreateWindowExW(0, L"EDIT", L"0.0",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            35, 65, 80, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditX)),
            instance, nullptr);

        self->yLabel_ = CreateWindowExW(0, L"STATIC", L"Y:",
            WS_CHILD | WS_VISIBLE, 125, 65, 20, 22,
            hwnd, nullptr, instance, nullptr);
        self->editY_ = CreateWindowExW(0, L"EDIT", L"0.0",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            150, 65, 80, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditY)),
            instance, nullptr);

        self->scaleLabel_ = CreateWindowExW(0, L"STATIC", L"Scale:",
            WS_CHILD | WS_VISIBLE, 10, 95, 50, 22,
            hwnd, nullptr, instance, nullptr);
        self->editScale_ = CreateWindowExW(0, L"EDIT", L"1.0",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            65, 95, 80, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditScale)),
            instance, nullptr);

        self->applyTransformButton_ = CreateWindowExW(0, L"BUTTON", L"Apply Transform",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            155, 95, 120, 24, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplyTransform)),
            instance, nullptr);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
        if (panel && header->hwndFrom == panel->tabControl_ &&
            header->code == TCN_SELCHANGE) {
            panel->onTabChanged();
        }
        return 0;
    }

    case WM_COMMAND:
        if (!panel) break;
        if (LOWORD(wParam) == kIdModel && HIWORD(wParam) == CBN_SELCHANGE) {
            panel->onModelSelected();
        } else if (LOWORD(wParam) == kIdLoad && HIWORD(wParam) == BN_CLICKED) {
            panel->onLoadModel();
        } else if (LOWORD(wParam) == kIdRemove && HIWORD(wParam) == BN_CLICKED) {
            panel->onRemoveModel();
        } else if (LOWORD(wParam) == kIdApply && HIWORD(wParam) == BN_CLICKED) {
            panel->onApplyAnimation();
        } else if (LOWORD(wParam) == kIdApplyTransform && HIWORD(wParam) == BN_CLICKED) {
            panel->onApplyTransform();
        } else if (LOWORD(wParam) == kIdList && HIWORD(wParam) == LBN_DBLCLK) {
            panel->onApplyAnimation();
        }
        return 0;

    case WM_CLOSE:
        if (panel && panel->state_) panel->state_->quit = true;
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ControlPanel::create(HINSTANCE instance, AppState* state) {
    state_ = state;

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kWindowClass, L"Pet Control Panel",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 420,
        nullptr, nullptr, instance, this);
    if (!hwnd_) {
        std::cerr << "CreateWindowExW failed, error: "
                  << GetLastError() << std::endl;
        return false;
    }

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    showPage(0);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    refreshModels();
    refreshAnimations();
    refreshTransformControls();
    return true;
}

void ControlPanel::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

void ControlPanel::showPage(int page) {
    currentPage_ = page;
    const bool page1 = (page == 0);

    ShowWindow(modelLabel_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(modelCombo_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(animLabel_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(listBox_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(applyAnimButton_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(loadButton_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(removeButton_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(statusText_, page1 ? SW_SHOW : SW_HIDE);

    ShowWindow(transformLabel_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(xLabel_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(editX_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(yLabel_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(editY_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(scaleLabel_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(editScale_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(applyTransformButton_, page1 ? SW_HIDE : SW_SHOW);
}

void ControlPanel::onTabChanged() {
    int index = TabCtrl_GetCurSel(tabControl_);
    showPage(index);
}

void ControlPanel::refreshModels() {
    if (!modelCombo_ || !state_) return;
    SendMessageW(modelCombo_, CB_RESETCONTENT, 0, 0);
    for (const auto& model : state_->models) {
        std::wstring wide = toWide(model.name);
        SendMessageW(modelCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
    if (state_->currentModelIndex >= 0 &&
        state_->currentModelIndex < static_cast<int>(state_->models.size())) {
        SendMessageW(modelCombo_, CB_SETCURSEL, state_->currentModelIndex, 0);
    }
}

void ControlPanel::refreshAnimations() {
    if (!listBox_ || !state_) return;
    SendMessageW(listBox_, LB_RESETCONTENT, 0, 0);
    for (const std::string& name : state_->animations) {
        std::wstring wide = toWide(name);
        SendMessageW(listBox_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
    if (!state_->animations.empty()) {
        SendMessageW(listBox_, LB_SETCURSEL, 0, 0);
    }
    setCurrentAnimation(state_->currentAnimation);
}

void ControlPanel::setCurrentAnimation(const std::string& name) {
    if (state_) state_->currentAnimation = name;
    if (statusText_) {
        std::wstring text = L"Current: " + toWide(name);
        SetWindowTextW(statusText_, text.c_str());
    }
}

void ControlPanel::refreshTransformControls() {
    if (!state_ || !editX_ || !editY_ || !editScale_) return;

    wchar_t buffer[32];
    swprintf_s(buffer, L"%.1f", state_->positionX);
    SetWindowTextW(editX_, buffer);
    swprintf_s(buffer, L"%.1f", state_->positionY);
    SetWindowTextW(editY_, buffer);
    swprintf_s(buffer, L"%.2f", state_->scale);
    SetWindowTextW(editScale_, buffer);
}

void ControlPanel::onApplyTransform() {
    if (!state_ || !state_->model || !editX_ || !editY_ || !editScale_) return;

    wchar_t buffer[64];
    GetWindowTextW(editX_, buffer, 64);
    float x = static_cast<float>(_wtof(buffer));
    GetWindowTextW(editY_, buffer, 64);
    float y = static_cast<float>(_wtof(buffer));
    GetWindowTextW(editScale_, buffer, 64);
    float scale = static_cast<float>(_wtof(buffer));

    if (scale < 0.05f) scale = 0.05f;
    if (scale > 5.0f) scale = 5.0f;

    state_->positionX = x;
    state_->positionY = y;
    state_->scale = scale;

    state_->model->setPosition(x, y);
    state_->model->setScale(scale);
    refreshTransformControls();
}

void ControlPanel::onModelSelected() {
    if (!modelCombo_ || !state_ || !state_->model) return;
    int index = static_cast<int>(SendMessageW(modelCombo_, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(state_->models.size())) return;
    if (index == state_->currentModelIndex) return;

    const ModelConfig& config = state_->models[index];
    if (!state_->model->load(config.atlasPath.c_str(), config.skeletonPath.c_str())) {
        std::cerr << "Failed to load model: " << config.name << std::endl;
        return;
    }

    state_->currentModelIndex = index;
    state_->animations = state_->model->getAnimationNames();
    state_->currentAnimation.clear();
    if (!state_->animations.empty()) {
        state_->currentAnimation = state_->animations[0];
        state_->model->setAnimation(state_->currentAnimation.c_str(), true);
    }
    refreshAnimations();
}

void ControlPanel::onLoadModel() {
    if (!state_ || !state_->model) return;

    wchar_t skeletonFile[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Spine Skeleton (*.skel;*.json)\0*.skel;*.json\0All Files\0*.*\0";
    ofn.lpstrFile = skeletonFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    std::string skeletonPath = toNarrow(skeletonFile);
    std::string base = skeletonPath;
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    std::string atlasPath = base + ".atlas";

    if (GetFileAttributesW(toWide(atlasPath).c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t atlasFile[MAX_PATH] = {};
        OPENFILENAMEW atlasOfn = {};
        atlasOfn.lStructSize = sizeof(atlasOfn);
        atlasOfn.hwndOwner = hwnd_;
        atlasOfn.lpstrFilter = L"Spine Atlas (*.atlas)\0*.atlas\0All Files\0*.*\0";
        atlasOfn.lpstrFile = atlasFile;
        atlasOfn.nMaxFile = MAX_PATH;
        atlasOfn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&atlasOfn)) return;
        atlasPath = toNarrow(atlasFile);
    }

    std::string name = base;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);

    const std::string assetsRoot = assetRoot() + "\\spine";
    ModelConfig config;
    bool addedNew = false;
    bool insideAssets = atlasPath.find(assetsRoot) == 0;
    if (insideAssets) {
        int existing = -1;
        for (int i = 0; i < static_cast<int>(state_->models.size()); ++i) {
            if (state_->models[i].name == name &&
                state_->models[i].atlasPath == atlasPath &&
                state_->models[i].skeletonPath == skeletonPath) {
                existing = i;
                break;
            }
        }
        if (existing >= 0) {
            config = state_->models[existing];
            state_->currentModelIndex = existing;
        } else {
            config = { name, atlasPath, skeletonPath };
            addedNew = true;
        }
    } else {
        name = uniqueModelName(state_->models, name);
        ModelConfig source = { name, atlasPath, skeletonPath };
        if (!importModelFiles(source, assetsRoot, config)) {
            std::cerr << "Failed to import model files: " << name << std::endl;
            config = source;
        }
        addedNew = true;
    }

    if (!state_->model->load(config.atlasPath.c_str(), config.skeletonPath.c_str())) {
        std::cerr << "Failed to load model: " << config.name << std::endl;
        return;
    }

    if (addedNew) {
        state_->models.push_back(config);
        state_->currentModelIndex = static_cast<int>(state_->models.size()) - 1;
    }
    state_->animations = state_->model->getAnimationNames();
    state_->currentAnimation.clear();
    if (!state_->animations.empty()) {
        state_->currentAnimation = state_->animations[0];
        state_->model->setAnimation(state_->currentAnimation.c_str(), true);
    }
    saveModelRegistry(assetRoot() + "\\spine\\models.txt", state_->models);
    refreshModels();
    refreshAnimations();
}

void ControlPanel::onRemoveModel() {
    if (!state_ || !state_->model) return;
    if (state_->currentModelIndex < 0 ||
        state_->currentModelIndex >= static_cast<int>(state_->models.size())) return;

    int removedIndex = state_->currentModelIndex;
    ModelConfig removedConfig = state_->models[removedIndex];
    state_->models.erase(state_->models.begin() + removedIndex);
    state_->animations.clear();
    state_->currentAnimation.clear();

    if (state_->models.empty()) {
        state_->currentModelIndex = -1;
        state_->model->unload();
    } else {
        int next = removedIndex < static_cast<int>(state_->models.size())
                       ? removedIndex
                       : 0;
        state_->currentModelIndex = next;
        const ModelConfig& config = state_->models[next];
        if (state_->model->load(config.atlasPath.c_str(), config.skeletonPath.c_str())) {
            state_->animations = state_->model->getAnimationNames();
            if (!state_->animations.empty()) {
                state_->currentAnimation = state_->animations[0];
                state_->model->setAnimation(state_->currentAnimation.c_str(), true);
            }
        } else {
            state_->currentModelIndex = -1;
            state_->model->unload();
        }
    }

    saveModelRegistry(assetRoot() + "\\spine\\models.txt", state_->models);
    removeModelFiles(removedConfig);
    refreshModels();
    refreshAnimations();
}

void ControlPanel::onApplyAnimation() {
    if (!listBox_ || !state_ || !state_->model) return;
    int index = static_cast<int>(SendMessageW(listBox_, LB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(state_->animations.size())) return;
    const std::string& name = state_->animations[index];
    if (state_->model->setAnimation(name.c_str(), true)) {
        setCurrentAnimation(name);
    }
}
