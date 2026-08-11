#pragma once

#include <string>
#include <windows.h>

struct AppState;

class ControlPanel {
public:
    bool create(HINSTANCE instance, AppState* state);
    void destroy();
    void refreshModels();
    void refreshAnimations();
    void setCurrentAnimation(const std::string& name);
    void refreshTransformControls();
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void onLoadModel();
    void onRemoveModel();
    void onModelSelected();
    void onApplyAnimation();
    void onApplyTransform();
    void onTabChanged();
    void showPage(int page);

    HWND hwnd_ = nullptr;
    HWND tabControl_ = nullptr;
    HWND modelLabel_ = nullptr;
    HWND modelCombo_ = nullptr;
    HWND animLabel_ = nullptr;
    HWND listBox_ = nullptr;
    HWND applyAnimButton_ = nullptr;
    HWND loadButton_ = nullptr;
    HWND removeButton_ = nullptr;
    HWND statusText_ = nullptr;
    HWND transformLabel_ = nullptr;
    HWND xLabel_ = nullptr;
    HWND editX_ = nullptr;
    HWND yLabel_ = nullptr;
    HWND editY_ = nullptr;
    HWND scaleLabel_ = nullptr;
    HWND editScale_ = nullptr;
    HWND applyTransformButton_ = nullptr;
    AppState* state_ = nullptr;
    int currentPage_ = 0;
};
