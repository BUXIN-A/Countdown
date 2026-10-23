cmake_minimum_required(VERSION 3.15)
project(Countdown LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态链接 CRT：单 exe 分发，无需 VC++ 运行时
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(SOURCES
    src/main.cpp
    src/config.cpp
    src/utils.cpp
    src/autostart.cpp
    src/countdown_window.cpp
    src/article_window.cpp
    src/settings_window.cpp
    src/hitokoto.cpp
    src/tray.cpp
    resource.rc
    app.manifest
)

add_executable(Countdown WIN32 ${SOURCES})

target_include_directories(Countdown PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 定义 UNICODE/_UNICODE：windowsx.h 的控件宏（ComboBox_AddString 等）依赖
# 这些宏走 W（宽字符）路径，否则 SendMessage 展开为 ANSI 版，宽字符串
# 被按字节读取导致只存下首字符；NOMINMAX 防止 min/max 宏冲突
target_compile_definitions(Countdown PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
    # /utf-8 保证源码 UTF-8 解析；/W3 控制告警噪音；/permissive- 严格标准
    target_compile_options(Countdown PRIVATE /utf-8 /W3 /permissive-)
    # 让 rc.exe 能找到 resource.h 与 countdown.ico
    set_source_files_properties(resource.rc PROPERTIES
        COMPILE_FLAGS "/I \"${CMAKE_CURRENT_SOURCE_DIR}\"")
elseif(MINGW)
    # 入口是 wWinMain：MinGW 必须加 -municode，否则链接报 undefined WinMain
    # -static 把 libstdc++/libgcc/winpthread 静态链入，保持单 exe 分发
    target_link_options(Countdown PRIVATE -municode -static)
    # MinGW 不会像 MSVC 那样自动把 .manifest 合并进 exe，
    # 由 resource.rc 按该宏把清单编译成 RT_MANIFEST 资源
    target_compile_definitions(Countdown PRIVATE COUNTDOWN_EMBED_MANIFEST)
endif()

target_link_libraries(Countdown PRIVATE
    user32
    gdi32
    msimg32
    comctl32
    comdlg32
    dwmapi
    shell32
    uxtheme
    winhttp
)

# 输出名称
set_target_properties(Countdown PROPERTIES
    OUTPUT_NAME "Countdown")
