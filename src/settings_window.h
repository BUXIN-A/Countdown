// =============================================================================
// settings_window.h - 设置窗口
//
// 模态自定义窗口，四标签页（基础设置 / 倒计时 / 一文 / 关于）。
// 保存后写回 config.json；返回 true 时调用方应用新配置。
// =============================================================================
#pragma once
#include <windows.h>
#include "config.h"

// 打开模态设置窗口；返回 true 表示用户已保存（cfg 已更新）。
bool OpenSettingsDialog(HWND parent, Config& cfg);

// 关闭当前打开的设置窗口（若有）。退出流程必须先调用它，否则模态消息循环
// 会吞掉 WM_QUIT，导致进程无法正常退出。
void CloseSettingsWindow();
