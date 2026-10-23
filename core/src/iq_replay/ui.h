#pragma once
#include "replay.h"
#include <imgui.h>

namespace beige::iqreplay {
void resizeRows(void* owner, int oldHead, int oldHeight, int height);
void clearRows(void* owner);
void pushRow(void* owner, int index, int height);
bool handleInput(void* owner, ImVec2 min, ImVec2 max, const std::string& receiver);
void drawMap(void* owner, ImDrawList* draw, ImVec2 min, ImVec2 max, int head, int filled,
    int height, double low, double high);
void drawSetting();
void clearMaps();
}
