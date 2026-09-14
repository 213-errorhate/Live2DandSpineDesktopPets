#include "ControlPanel.h"
#include "AppSettings.h"
#include "AppState.h"
#include "ModelRegistry.h"
#include "PathUtils.h"
#include "Live2DModel.h"
#include "SpineModel.h"

#include <algorithm>
#include <cctype>
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
const int kIdSkin = 1009;
const int kIdApplySkin = 1010;
const int kIdLoop = 1011;
const int kIdPma = 1012;
const int kIdEditSpeed = 1013;
const int kIdEditMix = 1014;
const int kIdApplyPlayback = 1015;
const int kIdAlwaysOnTop = 1016;
const int kIdMouseDrag = 1017;
const int kIdApplyOverlay = 1018;
const int kIdClearOverlays = 1019;

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

bool endsWithInsensitive(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(),
        [](unsigned char left, unsigned char right) {
            return std::tolower(left) == std::tolower(right);
        });
}

bool currentModelLoaded(const AppState* state) {
    if (!state) return false;
    if (state->currentModelType == PetModelType::Live2D)
        return state->live2dModel && state->live2dModel->loaded();
    return state->model && state->model->loaded();
}

const std::string& modelErrorForType(const AppState* state, PetModelType type) {
    static const std::string empty;
    if (!state) return empty;
    if (type == PetModelType::Live2D)
        return state->live2dModel ? state->live2dModel->lastError() : empty;
    return state->model ? state->model->lastError() : empty;
}

bool loadConfiguredModel(AppState* state, const ModelConfig& config) {
    if (!state || !state->model || !state->live2dModel) return false;
    if (config.type == PetModelType::Live2D) {
        if (!state->live2dModel->load(config.skeletonPath.c_str(),
                state->renderWidth, state->renderHeight)) {
            return false;
        }
        state->model->unload();
        state->currentModelType = PetModelType::Live2D;
        return true;
    }

    if (!state->model->load(config.atlasPath.c_str(), config.skeletonPath.c_str()))
        return false;
    state->live2dModel->unload();
    state->currentModelType = PetModelType::Spine;
    return true;
}

void syncLoadedModelState(AppState* state, const ModelConfig& config) {
    if (!state || !state->model || !state->live2dModel) return;
    state->premultipliedAlpha = config.premultipliedAlpha;
    state->currentAnimation.clear();
    state->overlayAnimations.clear();
    state->currentSkin.clear();

    if (state->currentModelType == PetModelType::Live2D) {
        state->animations = state->live2dModel->getAnimationNames();
        state->skins = state->live2dModel->getExpressionNames();
        state->live2dModel->setTimeScale(state->animationSpeed);
    } else {
        state->animations = state->model->getAnimationNames();
        state->skins = state->model->getSkinNames();
        state->model->setDefaultMix(state->animationMix);
        state->model->setTimeScale(state->animationSpeed);
    }
    if (!state->skins.empty()) {
        state->currentSkin = state->skins[0];
        if (state->currentModelType == PetModelType::Live2D)
            state->live2dModel->setExpression(state->currentSkin.c_str());
        else
            state->model->setSkin(state->currentSkin.c_str());
    }
    if (!state->animations.empty()) {
        state->currentAnimation = state->animations[0];
        if (state->currentModelType == PetModelType::Live2D)
            state->live2dModel->setAnimation(
                state->currentAnimation.c_str(), state->animationLoop);
        else
            state->model->setAnimation(
                state->currentAnimation.c_str(), state->animationLoop);
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

        self->applyAnimButton_ = CreateWindowExW(0, L"BUTTON", L"主轨播放",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 315, 80, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApply)),
            instance, nullptr);

        self->applyOverlayButton_ = CreateWindowExW(0, L"BUTTON", L"叠加播放",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            95, 315, 80, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplyOverlay)),
            instance, nullptr);

        self->clearOverlaysButton_ = CreateWindowExW(0, L"BUTTON", L"清除叠加",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            180, 315, 100, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdClearOverlays)),
            instance, nullptr);

        self->loadButton_ = CreateWindowExW(0, L"BUTTON", L"Load Model...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 355, 130, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLoad)),
            instance, nullptr);

        self->removeButton_ = CreateWindowExW(0, L"BUTTON", L"Remove",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            150, 355, 130, 32, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRemove)),
            instance, nullptr);

        self->statusText_ = CreateWindowExW(0, L"STATIC", L"Current: none",
            WS_CHILD | WS_VISIBLE, 10, 395, 270, 25,
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

        self->skinLabel_ = CreateWindowExW(0, L"STATIC", L"Skin:",
            WS_CHILD | WS_VISIBLE, 10, 140, 50, 22,
            hwnd, nullptr, instance, nullptr);
        self->skinCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            65, 138, 145, 160, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSkin)),
            instance, nullptr);
        self->applySkinButton_ = CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            215, 138, 60, 24, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplySkin)),
            instance, nullptr);

        self->loopCheck_ = CreateWindowExW(0, L"BUTTON", L"Loop animation",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            10, 180, 130, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLoop)),
            instance, nullptr);
        self->pmaCheck_ = CreateWindowExW(0, L"BUTTON", L"PMA atlas texture",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            145, 180, 135, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPma)),
            instance, nullptr);

        self->speedLabel_ = CreateWindowExW(0, L"STATIC", L"Speed:",
            WS_CHILD | WS_VISIBLE, 10, 215, 50, 22,
            hwnd, nullptr, instance, nullptr);
        self->editSpeed_ = CreateWindowExW(0, L"EDIT", L"1.0",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            65, 215, 70, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditSpeed)),
            instance, nullptr);
        self->mixLabel_ = CreateWindowExW(0, L"STATIC", L"Mix (s):",
            WS_CHILD | WS_VISIBLE, 145, 215, 60, 22,
            hwnd, nullptr, instance, nullptr);
        self->editMix_ = CreateWindowExW(0, L"EDIT", L"0.2",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP,
            210, 215, 65, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditMix)),
            instance, nullptr);
        self->applyPlaybackButton_ = CreateWindowExW(0, L"BUTTON", L"Apply Playback Settings",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 250, 265, 28, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApplyPlayback)),
            instance, nullptr);
        self->alwaysOnTopCheck_ = CreateWindowExW(0, L"BUTTON", L"桌宠始终置顶",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            10, 290, 150, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAlwaysOnTop)),
            instance, nullptr);
        self->mouseDragCheck_ = CreateWindowExW(0, L"BUTTON", L"允许鼠标拖动桌宠",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            10, 320, 180, 22, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMouseDrag)),
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
        } else if (LOWORD(wParam) == kIdApplyOverlay && HIWORD(wParam) == BN_CLICKED) {
            panel->onApplyOverlayAnimation();
        } else if (LOWORD(wParam) == kIdClearOverlays && HIWORD(wParam) == BN_CLICKED) {
            panel->onClearOverlayAnimations();
        } else if (LOWORD(wParam) == kIdApplyTransform && HIWORD(wParam) == BN_CLICKED) {
            panel->onApplyTransform();
        } else if (LOWORD(wParam) == kIdApplySkin && HIWORD(wParam) == BN_CLICKED) {
            panel->onApplySkin();
        } else if (LOWORD(wParam) == kIdApplyPlayback && HIWORD(wParam) == BN_CLICKED) {
            panel->onApplyPlayback();
        } else if (LOWORD(wParam) == kIdAlwaysOnTop && HIWORD(wParam) == BN_CLICKED) {
            panel->onAlwaysOnTopChanged();
        } else if (LOWORD(wParam) == kIdMouseDrag && HIWORD(wParam) == BN_CLICKED) {
            panel->onMouseDragChanged();
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
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 465,
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
    refreshSkins();
    refreshTransformControls();
    refreshPlaybackControls();
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
    ShowWindow(applyOverlayButton_, page1 ? SW_SHOW : SW_HIDE);
    ShowWindow(clearOverlaysButton_, page1 ? SW_SHOW : SW_HIDE);
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
    ShowWindow(skinLabel_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(skinCombo_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(applySkinButton_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(loopCheck_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(pmaCheck_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(speedLabel_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(editSpeed_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(mixLabel_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(editMix_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(applyPlaybackButton_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(alwaysOnTopCheck_, page1 ? SW_HIDE : SW_SHOW);
    ShowWindow(mouseDragCheck_, page1 ? SW_HIDE : SW_SHOW);
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
    const bool spineModel = state_->currentModelType == PetModelType::Spine;
    if (applyOverlayButton_) EnableWindow(applyOverlayButton_, spineModel);
    if (clearOverlaysButton_) {
        EnableWindow(clearOverlaysButton_,
            spineModel && !state_->overlayAnimations.empty());
    }
    setCurrentAnimation(state_->currentAnimation);
}

void ControlPanel::setCurrentAnimation(const std::string& name) {
    if (state_) state_->currentAnimation = name;
    refreshAnimationStatus();
}

void ControlPanel::refreshAnimationStatus() {
    if (!statusText_ || !state_) return;
    std::wstring text = L"主轨: ";
    text += state_->currentAnimation.empty() ? L"无" : toWide(state_->currentAnimation);
    if (!state_->overlayAnimations.empty()) {
        text += L" | 叠加: ";
        for (size_t index = 0; index < state_->overlayAnimations.size(); ++index) {
            if (index > 0) text += L", ";
            text += toWide(state_->overlayAnimations[index]);
        }
    }
    SetWindowTextW(statusText_, text.c_str());
}

void ControlPanel::refreshSkins() {
    if (!skinCombo_ || !state_) return;
    SetWindowTextW(skinLabel_, state_->currentModelType == PetModelType::Live2D
        ? L"Expression:" : L"Skin:");
    SendMessageW(skinCombo_, CB_RESETCONTENT, 0, 0);
    int selected = -1;
    for (int i = 0; i < static_cast<int>(state_->skins.size()); ++i) {
        std::wstring wide = toWide(state_->skins[i]);
        SendMessageW(skinCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
        if (state_->skins[i] == state_->currentSkin) selected = i;
    }
    if (selected < 0 && !state_->skins.empty()) selected = 0;
    if (selected >= 0) SendMessageW(skinCombo_, CB_SETCURSEL, selected, 0);
}

void ControlPanel::refreshPlaybackControls() {
    if (!state_ || !loopCheck_ || !pmaCheck_ || !editSpeed_ || !editMix_ ||
        !alwaysOnTopCheck_ || !mouseDragCheck_) return;
    SendMessageW(loopCheck_, BM_SETCHECK,
        state_->animationLoop ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(pmaCheck_, BM_SETCHECK,
        state_->premultipliedAlpha ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(pmaCheck_, state_->currentModelType == PetModelType::Spine);
    EnableWindow(editMix_, state_->currentModelType == PetModelType::Spine);
    EnableWindow(mixLabel_, state_->currentModelType == PetModelType::Spine);
    SendMessageW(alwaysOnTopCheck_, BM_SETCHECK,
        state_->alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(mouseDragCheck_, BM_SETCHECK,
        state_->mouseDragEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    wchar_t buffer[32];
    swprintf_s(buffer, L"%.2f", state_->animationSpeed);
    SetWindowTextW(editSpeed_, buffer);
    swprintf_s(buffer, L"%.2f", state_->animationMix);
    SetWindowTextW(editMix_, buffer);
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
    if (!state_ || !editX_ || !editY_ || !editScale_) return;

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

    if (state_->currentModelType == PetModelType::Spine && state_->model) {
        state_->model->setPosition(x, y);
        state_->model->setScale(scale);
    }
    refreshTransformControls();
    saveAppSettings(*state_);
}

void ControlPanel::onApplySkin() {
    if (!state_ || !skinCombo_ || !currentModelLoaded(state_)) return;
    int index = static_cast<int>(SendMessageW(skinCombo_, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(state_->skins.size())) return;
    const std::string& skin = state_->skins[index];
    const bool applied = state_->currentModelType == PetModelType::Live2D
        ? state_->live2dModel->setExpression(skin.c_str())
        : state_->model->setSkin(skin.c_str());
    if (applied) {
        state_->currentSkin = skin;
        saveAppSettings(*state_);
    }
}

void ControlPanel::onApplyPlayback() {
    if (!state_ || !editSpeed_ || !editMix_ || !currentModelLoaded(state_)) return;
    wchar_t buffer[64];
    GetWindowTextW(editSpeed_, buffer, 64);
    float speed = static_cast<float>(_wtof(buffer));
    GetWindowTextW(editMix_, buffer, 64);
    float mix = static_cast<float>(_wtof(buffer));
    if (speed < 0.0f) speed = 0.0f;
    if (speed > 5.0f) speed = 5.0f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 5.0f) mix = 5.0f;
    state_->animationSpeed = speed;
    state_->animationMix = mix;
    state_->animationLoop = SendMessageW(loopCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (state_->currentModelType == PetModelType::Live2D) {
        state_->live2dModel->setTimeScale(speed);
        state_->live2dModel->setCurrentLoop(state_->animationLoop);
    } else {
        state_->premultipliedAlpha =
            SendMessageW(pmaCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state_->model->setTimeScale(speed);
        state_->model->setDefaultMix(mix);
        state_->model->setCurrentLoop(state_->animationLoop);
        for (size_t index = 0; index < state_->overlayAnimations.size(); ++index)
            state_->model->setCurrentLoop(state_->animationLoop, static_cast<int>(index) + 1);
    }
    if (state_->currentModelIndex >= 0 &&
        state_->currentModelIndex < static_cast<int>(state_->models.size())) {
        if (state_->currentModelType == PetModelType::Spine)
            state_->models[state_->currentModelIndex].premultipliedAlpha = state_->premultipliedAlpha;
        saveModelRegistry(assetRoot() + "\\spine\\models.txt", state_->models);
    }
    refreshPlaybackControls();
    saveAppSettings(*state_);
}

void ControlPanel::onAlwaysOnTopChanged() {
    if (!state_ || !alwaysOnTopCheck_) return;
    state_->alwaysOnTop =
        SendMessageW(alwaysOnTopCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state_->alwaysOnTopChanged = true;
    saveAppSettings(*state_);
}

void ControlPanel::onMouseDragChanged() {
    if (!state_ || !mouseDragCheck_) return;
    state_->mouseDragEnabled =
        SendMessageW(mouseDragCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    saveAppSettings(*state_);
}

void ControlPanel::onModelSelected() {
    if (!modelCombo_ || !state_ || !state_->model || !state_->live2dModel) return;
    int index = static_cast<int>(SendMessageW(modelCombo_, CB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(state_->models.size())) return;
    if (index == state_->currentModelIndex && currentModelLoaded(state_)) return;

    const ModelConfig& config = state_->models[index];
    if (!loadConfiguredModel(state_, config)) {
        std::cerr << "Failed to load model: " << config.name << std::endl;
        std::wstring error = toWide(modelErrorForType(state_, config.type));
        MessageBoxW(hwnd_, error.c_str(), L"Pet model load failed", MB_OK | MB_ICONERROR);
        SendMessageW(modelCombo_, CB_SETCURSEL, state_->currentModelIndex, 0);
        return;
    }

    state_->currentModelIndex = index;
    syncLoadedModelState(state_, config);
    refreshAnimations();
    refreshSkins();
    refreshPlaybackControls();
    saveAppSettings(*state_);
}

void ControlPanel::onLoadModel() {
    if (!state_ || !state_->model || !state_->live2dModel) return;

    wchar_t skeletonFile[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter =
        L"Pet Model (*.skel;*.json;*.model3.json)\0*.skel;*.json;*.model3.json\0"
        L"Live2D Model (*.model3.json)\0*.model3.json\0"
        L"Spine Skeleton (*.skel;*.json)\0*.skel;*.json\0All Files\0*.*\0";
    ofn.lpstrFile = skeletonFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    std::string skeletonPath = toNarrow(skeletonFile);
    const bool isLive2D = endsWithInsensitive(skeletonPath, ".model3.json");
    std::string base = skeletonPath;
    if (isLive2D)
        base.resize(base.size() - std::string(".model3.json").size());
    else {
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);
    }
    std::string atlasPath = isLive2D ? "" : base + ".atlas";

    if (!isLive2D &&
        GetFileAttributesW(toWide(atlasPath).c_str()) == INVALID_FILE_ATTRIBUTES) {
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

    const std::string assetsRoot = assetRoot() + (isLive2D ? "\\live2d" : "\\spine");
    ModelConfig config;
    bool addedNew = false;
    int existingIndex = -1;
    bool insideAssets = isPathInsideDirectory(
        isLive2D ? skeletonPath : atlasPath, assetsRoot);
    if (insideAssets) {
        int existing = -1;
        for (int i = 0; i < static_cast<int>(state_->models.size()); ++i) {
            if (state_->models[i].type == (isLive2D ? PetModelType::Live2D : PetModelType::Spine) &&
                state_->models[i].name == name &&
                state_->models[i].atlasPath == atlasPath &&
                state_->models[i].skeletonPath == skeletonPath) {
                existing = i;
                break;
            }
        }
        if (existing >= 0) {
            config = state_->models[existing];
            existingIndex = existing;
        } else {
            config = { name, atlasPath, skeletonPath };
            config.type = isLive2D ? PetModelType::Live2D : PetModelType::Spine;
            config.premultipliedAlpha = !isLive2D;
            addedNew = true;
        }
    } else if (isLive2D) {
        config.name = uniqueModelName(state_->models, name);
        config.skeletonPath = skeletonPath;
        config.premultipliedAlpha = false;
        config.type = PetModelType::Live2D;
        addedNew = true;
    } else {
        name = uniqueModelName(state_->models, name);
        ModelConfig source = { name, atlasPath, skeletonPath };
        if (!importModelFiles(source, assetsRoot, config)) {
            std::cerr << "Failed to import model files: " << name << std::endl;
            config = source;
        }
        addedNew = true;
    }

    if (!loadConfiguredModel(state_, config)) {
        std::cerr << "Failed to load model: " << config.name << std::endl;
        if (config.managedFiles) removeModelFiles(config);
        std::wstring error = toWide(modelErrorForType(state_, config.type));
        MessageBoxW(hwnd_, error.c_str(), L"Pet model load failed", MB_OK | MB_ICONERROR);
        return;
    }

    if (addedNew) {
        state_->models.push_back(config);
        state_->currentModelIndex = static_cast<int>(state_->models.size()) - 1;
    } else if (existingIndex >= 0) {
        state_->currentModelIndex = existingIndex;
    }
    syncLoadedModelState(state_, config);
    saveModelRegistry(assetRoot() + "\\spine\\models.txt", state_->models);
    refreshModels();
    refreshAnimations();
    refreshSkins();
    refreshPlaybackControls();
    saveAppSettings(*state_);
}

void ControlPanel::onRemoveModel() {
    if (!state_ || !state_->model || !state_->live2dModel) return;
    int removedIndex = state_->currentModelIndex;
    if (removedIndex < 0 || removedIndex >= static_cast<int>(state_->models.size()))
        removedIndex = static_cast<int>(SendMessageW(modelCombo_, CB_GETCURSEL, 0, 0));
    if (removedIndex < 0 || removedIndex >= static_cast<int>(state_->models.size())) return;

    const bool removingCurrent = removedIndex == state_->currentModelIndex;
    ModelConfig removedConfig = state_->models[removedIndex];
    state_->models.erase(state_->models.begin() + removedIndex);
    if (!removingCurrent) {
        if (state_->currentModelIndex > removedIndex) --state_->currentModelIndex;
        saveModelRegistry(assetRoot() + "\\spine\\models.txt", state_->models);
        removeModelFiles(removedConfig);
        refreshModels();
        saveAppSettings(*state_);
        return;
    }
    state_->animations.clear();
    state_->currentAnimation.clear();
    state_->skins.clear();
    state_->currentSkin.clear();

    if (state_->models.empty()) {
        state_->currentModelIndex = -1;
        state_->model->unload();
        state_->live2dModel->unload();
    } else {
        int next = removedIndex < static_cast<int>(state_->models.size())
                       ? removedIndex
                       : 0;
        state_->currentModelIndex = next;
        const ModelConfig& config = state_->models[next];
        if (loadConfiguredModel(state_, config)) {
            syncLoadedModelState(state_, config);
        } else {
            state_->currentModelIndex = -1;
            state_->model->unload();
            state_->live2dModel->unload();
        }
    }

    saveModelRegistry(assetRoot() + "\\spine\\models.txt", state_->models);
    removeModelFiles(removedConfig);
    refreshModels();
    refreshAnimations();
    refreshSkins();
    refreshPlaybackControls();
    saveAppSettings(*state_);
}

void ControlPanel::onApplyAnimation() {
    if (!listBox_ || !state_ || !currentModelLoaded(state_)) return;
    int index = static_cast<int>(SendMessageW(listBox_, LB_GETCURSEL, 0, 0));
    if (index < 0 || index >= static_cast<int>(state_->animations.size())) return;
    const std::string& name = state_->animations[index];
    const bool applied = state_->currentModelType == PetModelType::Live2D
        ? state_->live2dModel->setAnimation(name.c_str(), state_->animationLoop)
        : state_->model->setAnimation(name.c_str(), state_->animationLoop);
    if (applied) {
        setCurrentAnimation(name);
        saveAppSettings(*state_);
    }
}

void ControlPanel::onApplyOverlayAnimation() {
    if (!listBox_ || !state_ || state_->currentModelType != PetModelType::Spine ||
        !state_->model || !state_->model->loaded()) {
        return;
    }
    const int selection = static_cast<int>(SendMessageW(listBox_, LB_GETCURSEL, 0, 0));
    if (selection < 0 || selection >= static_cast<int>(state_->animations.size())) return;

    const std::string& name = state_->animations[selection];
    auto found = std::find(
        state_->overlayAnimations.begin(), state_->overlayAnimations.end(), name);
    const bool isNewLayer = found == state_->overlayAnimations.end();
    if (isNewLayer && state_->overlayAnimations.size() >= 31) {
        MessageBoxW(hwnd_, L"最多支持 31 个叠加动画。", L"叠加动画",
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    const int trackIndex = isNewLayer
        ? static_cast<int>(state_->overlayAnimations.size()) + 1
        : static_cast<int>(std::distance(state_->overlayAnimations.begin(), found)) + 1;
    if (!state_->model->setAnimation(name.c_str(), state_->animationLoop, trackIndex)) return;

    if (isNewLayer) state_->overlayAnimations.push_back(name);
    refreshAnimationStatus();
    if (clearOverlaysButton_) EnableWindow(clearOverlaysButton_, TRUE);
    saveAppSettings(*state_);
}

void ControlPanel::onClearOverlayAnimations() {
    if (!state_ || state_->currentModelType != PetModelType::Spine || !state_->model) return;
    for (size_t index = 0; index < state_->overlayAnimations.size(); ++index)
        state_->model->clearTrack(static_cast<int>(index) + 1);
    state_->overlayAnimations.clear();
    refreshAnimationStatus();
    if (clearOverlaysButton_) EnableWindow(clearOverlaysButton_, FALSE);
    saveAppSettings(*state_);
}
