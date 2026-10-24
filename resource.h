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
// 关闭月历浮层（从月历通知中返回后再销毁，避免在控件自身通知里销毁控件）
#define WM_APP_CLOSE_CALENDAR (WM_APP + 6)

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
// 循环 / 起始日 / 自启动
#define IDC_CHECK_CYCLE            3024
#define IDC_COMBO_CYCLE            3025
#define IDC_CHECK_INCLUDE_START    3026
#define IDC_CHECK_AUTOSTART        3027
// 一文栏目
#define IDC_CHECK_ARTICLE          3028
#define IDC_COMBO_ARTICLE_ANCHOR   3029
#define IDC_EDIT_ARTICLE_X         3031
#define IDC_EDIT_ARTICLE_Y         3032
#define IDC_EDIT_ARTICLE_H         3033
#define IDC_UD_ARTICLE_H           3034
#define IDC_EDIT_ARTICLE_M         3035
#define IDC_UD_ARTICLE_M           3036
#define IDC_EDIT_ARTICLE_S         3037
#define IDC_UD_ARTICLE_S           3038
#define IDC_CHECK_ARTICLE_TOP      3039
#define IDC_EDIT_ARTICLE_FONT      3040
#define IDC_UD_ARTICLE_FONT        3041
// 一文尺寸
#define IDC_COMBO_ARTICLE_SIZE_MODE 3042
#define IDC_EDIT_ARTICLE_WIDTH     3043
#define IDC_EDIT_ARTICLE_HEIGHT    3044
// 日期胶囊右侧的日历按钮与其弹出的月历
#define IDC_BTN_CALENDAR           3045
#define IDC_CAL_POPUP              3046
// 月历上方的年份切换按钮
#define IDC_CAL_PREV_YEAR          3050
#define IDC_CAL_NEXT_YEAR          3051
// 数字步进器与下拉浮层
#define IDC_BTN_STEP_MINUS         3047
#define IDC_BTN_STEP_PLUS          3048
#define IDC_DROPDOWN_LIST          3049
// 外观颜色
#define IDC_BTN_BG_COLOR           3052
#define IDC_BTN_TEXT_COLOR         3053
// 设置页标签
#define IDC_TAB_MAIN               3030
