// =============================================================================
// article_module_api.h - 一文显示模块（DLL）ABI
//
// 设计目标：
//  1. 模块只负责“选哪一篇”，文章池、去重记录、渲染仍由宿主维护；
//  2. 模块可配置项由 DLL 自带 schema 声明，宿主按类型动态生成设置界面，
//     新模块无需修改宿主代码；
//  3. ABI 只使用 C 结构体、UTF-16 字符串与 UTF-8 JSON，避免跨 DLL 传递 STL。
//
// 模块需导出以下函数（名称固定）：
//   ArticleModule_GetInfo
//   ArticleModule_GetConfigSchema
//   ArticleModule_Create
//   ArticleModule_Destroy
//   ArticleModule_ApplyConfig
//   ArticleModule_Select
//
// schema JSON 示例：
// {
//   "items": [
//     {
//       "key": "ranges",
//       "type": "dayRangeList",        // bool / int / choice / dayRangeList
//       "label": "时间段",
//       "hint": "同一时间段内的星期显示同一篇文章",
//       "max": 7,
//       "default": [{"start": 1, "end": 3}, {"start": 4, "end": 7}]
//     },
//     {
//       "key": "count", "type": "int", "label": "数量",
//       "min": 1, "max": 100, "step": 1, "default": 10
//     },
//     {
//       "key": "mode", "type": "choice", "label": "模式", "default": "a",
//       "options": [{"value": "a", "label": "模式 A"}]
//     }
//   ]
// }
// dayRangeList 的星期编号：1=周一 ... 7=周日，范围必须 start <= end 且不重叠。
// =============================================================================
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define ARTICLE_MODULE_ABI_VERSION 1

// 选择原因：启动/配置变更时尽量保持当前文章；定时刷新才主动换文
#define ARTICLE_SELECT_STARTUP 0
#define ARTICLE_SELECT_CONFIG  1
#define ARTICLE_SELECT_REFRESH 2

typedef struct ArticleModuleHostApi {
    int abi_version;
    void* host;

    // 文章池
    int (*article_count)(void* host);
    const char* (*article_key)(void* host, int index); // UTF-8 稳定键（哈希）

    // 已显示记录（由宿主实现，线程安全）
    int (*is_seen)(void* host, const char* key);
    void (*mark_seen)(void* host, const char* key);
    void (*clear_seen)(void* host);

    // 临时目录（不含末尾反斜杠）
    const wchar_t* (*temp_dir)(void* host);

    // 当前本地时间；weekday：1=周一 ... 7=周日
    void (*local_time)(void* host, int* year, int* month, int* day,
                       int* weekday, int* hour, int* minute, int* second);

    // 随机数
    unsigned int (*random_u32)(void* host);
} ArticleModuleHostApi;

typedef struct ArticleModuleInfo {
    int abi_version;
    const wchar_t* id;          // 配置中保存的模块标识，例如 "normal"
    const wchar_t* name;        // 设置页显示名称
    const wchar_t* description; // 一句话说明
} ArticleModuleInfo;

typedef const ArticleModuleInfo* (*ArticleModuleGetInfoFn)(void);
typedef const char* (*ArticleModuleGetSchemaFn)(void);
typedef void* (*ArticleModuleCreateFn)(const ArticleModuleHostApi* host);
typedef void (*ArticleModuleDestroyFn)(void* module);
typedef void (*ArticleModuleApplyConfigFn)(void* module, const char* json);
typedef int (*ArticleModuleSelectFn)(void* module, int current_index, int reason);

#ifdef __cplusplus
} // extern "C"

#define ARTICLE_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define ARTICLE_MODULE_EXPORT __declspec(dllexport)
#endif
