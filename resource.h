#pragma once

// =============================================================================
// 资源头文件：图标 ID、自定义窗口消息、对话框控件 ID
// =============================================================================

// 应用图标资源 ID
#define IDI_APPICON 100

// 自定义窗口消息
// 托盘图标回调消息
#define WM_APP_TRAY (WM_APP + 1)
// 打开设置窗口（编程接口，便于自动化测试及未来扩展如命令行参数/热键）
#define WM_APP_OPEN_SETTINGS (WM_APP + 3)
// 网络一言获取完成（后台线程通过 PostMessage 通知主窗口，lParam 指向 new std::wstring）
#define WM_APP_HITOKOTO (WM_APP + 4)

// 托盘右键菜单命令
#define IDM_TRAY_SETTINGS 2001
#define IDM_TRAY_EXIT     2002

// 设置窗口控件 ID
#define IDC_EDIT_TITLE       3002
#define IDC_DTP_DATE         3004
#define IDC_DTP_TIME         3005
#define IDC_COMBO_FORMAT     3007
#define IDC_EDIT_FONTSIZE    3009
#define IDC_UD_FONTSIZE      3010
#define IDC_COMBO_FONT       3012
#define IDC_BTN_SAVE         3013
#define IDC_BTN_CANCEL       3014
#define IDC_COMBO_HITOKOTO_SRC     3015
#define IDC_EDIT_HITOKOTO_REFRESH  3016
#define IDC_UD_HITOKOTO_REFRESH    3017
// 底部制作者信息（静态文本，无交互）
#define IDC_STATIC_CREDIT          3018
#define IDC_STATIC_CREDIT_MAIL     3019
// 窗口置顶 / 位置
#define IDC_CHECK_TOPMOST          3020
#define IDC_COMBO_ANCHOR           3021
#define IDC_EDIT_PLACE_X           3022
#define IDC_EDIT_PLACE_Y           3023
