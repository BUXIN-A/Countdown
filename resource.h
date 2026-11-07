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
// 一文模块配置对话框：增删时间段后重建布局（延后执行，避免销毁自身按钮）
#define WM_APP_REBUILD_MODULE (WM_APP + 7)
// 关闭星期选择浮层（延后执行，避免在列表自身通知里销毁列表）
#define WM_APP_CLOSE_DAY_POPUP (WM_APP + 8)

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
// 外观颜色（倒计时）
#define IDC_BTN_BG_COLOR           3052
#define IDC_BTN_TEXT_COLOR         3053
// 一言加粗 / 一文独立外观
#define IDC_BTN_HK_BOLD            3054
#define IDC_BTN_ART_BG_COLOR       3055
#define IDC_BTN_ART_TEXT_COLOR     3056
// 一文显示模块
#define IDC_BTN_ARTICLE_MODULE     3057
#define IDC_BTN_MODULE_CONFIG      3058
// 临时数据
#define IDC_BTN_TEMP_CLEAN         3059
#define IDC_EDIT_TEMP_ART_LIMIT    3060
#define IDC_EDIT_TEMP_HK_LIMIT     3061
#define IDC_EDIT_TEMP_TRIM_COUNT   3062
#define IDC_STATIC_ART_LIMIT_LABEL 3063
#define IDC_STATIC_HK_LIMIT_LABEL  3064
#define IDC_STATIC_MODULE_HINT     3065
// 关于页
#define IDC_BTN_UPDATE             3066
// 倒计时窗口尺寸与圆角
#define IDC_EDIT_CD_WIDTH          3067
#define IDC_EDIT_CD_HEIGHT         3068
// 设置页标签
#define IDC_TAB_MAIN               3030

// 一文模块配置对话框
#define IDC_MC_RANGES_ADD          3200
#define IDC_MC_OK                  3201
#define IDC_MC_CANCEL              3202
#define IDC_MC_DAY_POPUP           3203
#define IDC_MC_RANGE_DELETE_BASE   3220
#define IDC_MC_RANGE_START_BASE    3240
#define IDC_MC_RANGE_END_BASE      3260
#define IDC_MC_DAY_ITEM_BASE       3300
