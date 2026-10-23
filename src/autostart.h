// =============================================================================
// autostart.h - 开机自启动（HKCU Run）
// =============================================================================
#pragma once

namespace autostart {

// 写入/删除当前用户 Run 项；无需管理员权限
bool SetEnabled(bool enabled);

} // namespace autostart
