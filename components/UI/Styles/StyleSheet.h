/*
 * Material You Light Theme — StyleSheet
 * 360x360 circular display
 */

#ifndef STYLESHEET_H
#define STYLESHEET_H

#include "lvgl.h"

/* ═══════════════════════════════════════════
   Primary (seed: #6750A4 purple-blue)
   ═══════════════════════════════════════════ */
#define S_COLOR_PRIMARY lv_color_hex(0x6750A4)
#define S_COLOR_ON_PRIMARY lv_color_hex(0xFFFFFF)
#define S_COLOR_PRIMARY_CONTAINER lv_color_hex(0xEADDFF)
#define S_COLOR_ON_PRIMARY_CONTAINER lv_color_hex(0x21005D)

/* ═══════════════════════════════════════════
   Secondary
   ═══════════════════════════════════════════ */
#define S_COLOR_SECONDARY lv_color_hex(0x625B71)
#define S_COLOR_ON_SECONDARY lv_color_hex(0xFFFFFF)
#define S_COLOR_SECONDARY_CONTAINER lv_color_hex(0xE8DEF8)
#define S_COLOR_ON_SECONDARY_CONTAINER lv_color_hex(0x1D192B)

/* ═══════════════════════════════════════════
   Surface (elevation-based backgrounds)
   ═══════════════════════════════════════════ */
#define S_COLOR_BACKGROUND lv_color_hex(0xFFFBFE)
#define S_COLOR_ON_BACKGROUND lv_color_hex(0x1C1B1F)
#define S_COLOR_SURFACE lv_color_hex(0xFFFBFE)
#define S_COLOR_ON_SURFACE lv_color_hex(0x1C1B1F)
#define S_COLOR_SURFACE_VARIANT lv_color_hex(0xE7E0EC)
#define S_COLOR_ON_SURFACE_VARIANT lv_color_hex(0x49454F)

#define S_COLOR_SURFACE_CONTAINER lv_color_hex(0xF3EDF7)
#define S_COLOR_SURFACE_CONTAINER_HIGH lv_color_hex(0xECE6F0)

/* elevation layers */
#define S_COLOR_SURFACE_LOWEST lv_color_hex(0xF7F2FA)
#define S_COLOR_SURFACE_LOW lv_color_hex(0xF3EDF7)
#define S_COLOR_SURFACE_MID lv_color_hex(0xECE6F0)
#define S_COLOR_SURFACE_HIGH lv_color_hex(0xE6E0E9)
#define S_COLOR_SURFACE_HIGHEST lv_color_hex(0xE0DAE3)

/* ═══════════════════════════════════════════
   Outline
   ═══════════════════════════════════════════ */
#define S_COLOR_OUTLINE lv_color_hex(0x79747E)
#define S_COLOR_OUTLINE_VARIANT lv_color_hex(0xCAC4D0)

/* ═══════════════════════════════════════════
   Error
   ═══════════════════════════════════════════ */
#define S_COLOR_ERROR lv_color_hex(0xB3261E)
#define S_COLOR_ON_ERROR lv_color_hex(0xFFFFFF)
#define S_COLOR_ERROR_CONTAINER lv_color_hex(0xF9DEDC)

/* ═══════════════════════════════════════════
   Semantic aliases
   ═══════════════════════════════════════════ */
#define S_BG_MAIN S_COLOR_BACKGROUND
#define S_BG_CARD S_COLOR_SURFACE_LOW
#define S_BG_SIDEBAR S_COLOR_SURFACE_CONTAINER
#define S_TEXT_PRIMARY S_COLOR_ON_BACKGROUND
#define S_TEXT_SECONDARY S_COLOR_ON_SURFACE_VARIANT
#define S_TEXT_ON_DARK lv_color_hex(0xFFFFFF)
#define S_DIVIDER S_COLOR_OUTLINE_VARIANT

/* ═══════════════════════════════════════════
   Spacing & shape (360x360 circular)
   ═══════════════════════════════════════════ */
#define S_PAD_H 16
#define S_PAD_V 12
#define S_GAP 8
#define S_RADIUS_CARD 16
#define S_RADIUS_BTN 20
#define S_RADIUS_SM 8

#endif
