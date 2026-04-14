#ifndef DARK_HIGH_CONTRAST_COLORS_H
#define DARK_HIGH_CONTRAST_COLORS_H

#include "lvgl.h"

/* ========== 核心颜色 ========== */
#define DHC_PRIMARY lv_color_hex(0xDAFBB0)           // primary
#define DHC_ON_PRIMARY lv_color_hex(0x000000)        // onPrimary
#define DHC_PRIMARY_CONTAINER lv_color_hex(0xADCD86) // primaryContainer
#define DHC_ON_PRIMARY_CONTAINER lv_color_hex(0x050E00)

#define DHC_SECONDARY lv_color_hex(0xE9F4D5)
#define DHC_ON_SECONDARY lv_color_hex(0x000000)
#define DHC_SECONDARY_CONTAINER lv_color_hex(0xBCC7A9)
#define DHC_ON_SECONDARY_CONTAINER lv_color_hex(0x060D01)

#define DHC_TERTIARY lv_color_hex(0xC9F9F5)
#define DHC_ON_TERTIARY lv_color_hex(0x000000)
#define DHC_TERTIARY_CONTAINER lv_color_hex(0x9CCCC7)
#define DHC_ON_TERTIARY_CONTAINER lv_color_hex(0x000E0D)

/* ========== 背景与表面 ========== */
#define DHC_BACKGROUND lv_color_hex(0x12140E)    // background
#define DHC_ON_BACKGROUND lv_color_hex(0xE2E3D8) // onBackground
#define DHC_SURFACE lv_color_hex(0x12140E)       // surface
#define DHC_ON_SURFACE lv_color_hex(0xFFFFFF)    // onSurface (纯白)
#define DHC_SURFACE_VARIANT lv_color_hex(0x44483D)
#define DHC_ON_SURFACE_VARIANT lv_color_hex(0xFFFFFF)

/* ========== 状态与边框 ========== */
#define DHC_ERROR lv_color_hex(0xFFECE9)
#define DHC_ON_ERROR lv_color_hex(0x000000)
#define DHC_ERROR_CONTAINER lv_color_hex(0xFFAEA4)
#define DHC_ON_ERROR_CONTAINER lv_color_hex(0x220001)

#define DHC_OUTLINE lv_color_hex(0xEEF2E2)
#define DHC_OUTLINE_VARIANT lv_color_hex(0xC1C4B6)

/* ========== 表面容器（层次） ========== */
#define DHC_SURFACE_DIM lv_color_hex(0x12140E)
#define DHC_SURFACE_BRIGHT lv_color_hex(0x4F5149)
#define DHC_SURFACE_CONTAINER_LOWEST lv_color_hex(0x000000)
#define DHC_SURFACE_CONTAINER_LOW lv_color_hex(0x1E201A)
#define DHC_SURFACE_CONTAINER lv_color_hex(0x2F312A)
#define DHC_SURFACE_CONTAINER_HIGH lv_color_hex(0x3A3C35)
#define DHC_SURFACE_CONTAINER_HIGHEST lv_color_hex(0x454840)

/* ========== 逆色（用于浅色表面） ========== */
#define DHC_INVERSE_SURFACE lv_color_hex(0xE2E3D8)
#define DHC_INVERSE_ON_SURFACE lv_color_hex(0x000000)
#define DHC_INVERSE_PRIMARY lv_color_hex(0x364F17)

#endif