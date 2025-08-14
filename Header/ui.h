#pragma once
#include <Windows.h>

// 初始化UI
void InitUI(HWND hwnd);

// 渲染一帧UI
void RenderUI();

// 清理UI资源
void ShutdownUI();