// =============================================================================
// settings_window.h - 设置窗口
//
// 模态自定义窗口，五标签页（基本 / 外观 / 一言 / 一文 / 窗口与启动）。
// 保存后写回 config.json；返回 true 时调用方应用新配置。
// =============================================================================
#pragma once
#include <windows.h>
#include "config.h"

// 打开模态设置窗口；返回 true 表示用户已保存（cfg 已更新）。
bool OpenSettingsDialog(HWND parent, Config& cfg);
