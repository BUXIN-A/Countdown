// =============================================================================
// history.h - 已显示内容记录（temp 文件夹）
//
// 用于减少一言/一文的重复：
//  - 每次随机取到内容后，把内容哈希写入 temp 下对应的记录文件；
//  - 随机时优先跳过已记录的条目；
//  - 某一类内容全部看过后，清空对应记录文件，重新开始一轮。
//
// 本地一言、网络一言、一文使用彼此独立的记录文件，互不影响。
// =============================================================================
#pragma once
#include <string>
#include <vector>

namespace history {

enum class Kind {
    LocalHitokoto = 0,   // temp\hitokoto_local.txt
    NetworkHitokoto,     // temp\hitokoto_network.txt
    Article,             // temp\article.txt
};

// temp 目录：默认 <exe 目录>\temp；不可写时回退到
// %LOCALAPPDATA%\Countdown\temp。返回值不含末尾反斜杠。
const std::wstring& TempDir();

// 内容是否已显示过（按去空白后的内容哈希判断）
bool IsSeen(Kind kind, const std::wstring& text);

// 批量筛出 items 中尚未显示过的下标（一次加锁，适合本地一言/一文的长列表）
void CollectUnseen(Kind kind, const std::vector<std::wstring>& items,
                   std::vector<size_t>& out);

// 记录一条内容（重复记录自动忽略）
void MarkSeen(Kind kind, const std::wstring& text);

// 已记录的条目数（网络一言用它判断是否需要开始新一轮）
int SeenCount(Kind kind);

// 清空某一类的记录（删除对应文件）
void Clear(Kind kind);

} // namespace history
