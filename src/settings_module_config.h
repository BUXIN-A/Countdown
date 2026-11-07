// =============================================================================
// settings_module_config.h - 一文模块通用配置对话框
//
// 对话框内容完全由 DLL 的 schema JSON 决定，支持：
//   bool / int / choice / dayRangeList
// 宿主只负责按类型生成控件，新增模块无需改设置窗口代码。
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include "article_module_registry.h"

// 打开模块配置对话框；返回 true 时 outJson 为新的配置对象 JSON。
bool OpenArticleModuleConfigDialog(HWND parent,
                                   const ArticleModuleEntry& entry,
                                   const std::string& currentJson,
                                   std::string& outJson);
