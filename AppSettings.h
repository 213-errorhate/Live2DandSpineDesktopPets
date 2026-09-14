#pragma once

#include <string>

struct AppState;

std::string appSettingsPath();
bool loadAppSettings(AppState& state);
bool saveAppSettings(const AppState& state);
