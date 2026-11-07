// =============================================================================
// history.h - 已显示内容记录（temp 文件夹）
//
// 用于减少一言/一文的重复：
//  - 每次随机取到内容后，把内容哈希写入 temp 下对应的记录文件；
//  - 随机时优先跳过已记录的条目；
//  - 某一类内容全部看过后，清空对应记录文件，重新开始一轮；
//  - 可按配置限制记录项数或文件大小，达到上限后清空或删除最早 N 项。
//
// 本地一言、网络一言、一文使用彼此独立的记录文件，互不影响。
// =============================================================================
#pragma once
#include <string>
#include <vector>
#include "config.h"

namespace history {

enum class Kind {
    LocalHitokoto = 0,   // temp\hitokoto_local.txt
    NetworkHitokoto,     // temp\hitokoto_network.txt
    Article,             // temp\article.txt
};

// temp 目录：默认 <exe 目录>\temp；不可写时回退到
// %LOCALAPPDATA%\Countdown\temp。返回值不含末尾反斜杠。
const std::wstring& TempDir();

// 内容稳定键（FNV-1a 64 位十六进制字符串，按去空白后的内容计算）
std::string KeyOfText(const std::wstring& text);

bool IsSeen(Kind kind, const std::wstring& text);
void CollectUnseen(Kind kind, const std::vector<std::wstring>& items,
                   std::vector<size_t>& out);
void MarkSeen(Kind kind, const std::wstring& text);

// 直接按内容键查询/记录（一文模块只持有稳定哈希，不传全文）
bool IsSeenKey(Kind kind, const std::string& key);
void MarkSeenKey(Kind kind, const std::string& key);
int SeenCount(Kind kind);
void Clear(Kind kind);

// 应用临时数据策略：记录上限 + 达到上限后的处理方式
void ApplyConfig(const Config& cfg);

// 立即清空全部记录（设置页“立即清理”）
void ClearAll();

} // namespace history
