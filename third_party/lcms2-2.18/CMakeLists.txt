# LCMS2 - Little CMS (MIT-licensed color management engine)
#   P0-8.3 (2026-09-15): vendored as third_party for ICC profile embedding
#   26 .c files in src/, public API in include/lcms2.h
#   Build STATIC lib vistella_lcms2 (PUBLIC include path for vistella_media)
#
#   原始仓库: https://github.com/mm2/Little-CMS
#   版本: 2.18
#   许可: MIT
#
cmake_minimum_required(VERSION 3.19)
project(lcms2 LANGUAGES C)

# Public header
set(LCMS2_PUBLIC_HEADERS
    include/lcms2.h
    include/lcms2_plugin.h
)

# 26 .c files
file(GLOB LCMS2_SOURCES CONFIGURE_DEPENDS src/cms*.c)

add_library(vistella_lcms2 STATIC
    ${LCMS2_PUBLIC_HEADERS}
    ${LCMS2_SOURCES}
)

target_include_directories(vistella_lcms2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# LCMS2 默认 _cdecl + thread safe (CMS_NO_REGISTER_OPTIMIZATION=1 给 multi-thread)
if(MSVC)
    target_compile_definitions(vistella_lcms2 PRIVATE
        _CRT_SECURE_NO_WARNINGS
        CMS_NO_REGISTER_OPTIMIZATION=1
    )
endif()

# LCMS2 默认 x86 优化 (cmsxform.c 用汇编), MSVC 不支持 ASM, 用 pure C fallback
#   项目里不要让 lcms2 用 SIMD intrinsics, 保持 cross-platform 兼容
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(vistella_lcms2 PRIVATE /w)  # 静默 warning (LCMS2 源码有 POSIX warning)
endif()