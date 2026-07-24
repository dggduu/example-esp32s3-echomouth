/*
 * Debug page — circular screen adapted
 */

#include "StyleSheet.h"
#include "cam_helper.h"
#include "core/lv_obj_style_gen.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "face_detector_helper.h"
#include "font/lv_symbol_def.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gs_nav.h"
#include "lv_conf_internal.h"
#include "lvgl.h"
#include "misc/lv_color.h"
#include "monitor_mamager.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TAG "DEBUG_PAGE"

#define CAM_W 320
#define CAM_H 240

#define PREVIEW_W 160
#define PREVIEW_H 120

#define SCALE_FACTOR ((PREVIEW_W * 256) / CAM_W)

typedef struct {
  lv_obj_t *root;
  lv_obj_t *img_obj;
  lv_image_dsc_t img_dsc;
  uint16_t *fb_buf;
  size_t fb_buf_size;
} fd_preview_ctx_t;

/* 初始化回调 */
static void *fd_preview_init(void *args) {
  fd_preview_ctx_t *ctx = calloc(1, sizeof(fd_preview_ctx_t));
  if (!ctx) {
    ESP_LOGE(TAG, "Failed to allocate context");
    return NULL;
  }

  size_t buf_size = CAM_W * CAM_H * sizeof(uint16_t);
  ctx->fb_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ctx->fb_buf) {
    ESP_LOGE(TAG, "Failed to allocate frame buffer");
    free(ctx);
    return NULL;
  }
  ctx->fb_buf_size = buf_size;
  memset(ctx->fb_buf, 0, buf_size);

  ctx->img_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  ctx->img_dsc.header.w = CAM_W;
  ctx->img_dsc.header.h = CAM_H;
  ctx->img_dsc.header.stride = CAM_W * 2;
  ctx->img_dsc.data_size = buf_size;
  ctx->img_dsc.data = (const uint8_t *)ctx->fb_buf;

  // 启动人脸检测（自动注册 cam_helper 订阅，开启摄像头）
  face_detector_helper_start_continuous();

  ESP_LOGI(TAG, "Face detection preview initialized");
  return ctx;
}

/* 渲染回调 */
static lv_obj_t *fd_preview_render(lv_obj_t *parent, void *ctx_in) {
  fd_preview_ctx_t *ctx = (fd_preview_ctx_t *)ctx_in;
  if (!ctx)
    return NULL;

  ctx->root = lv_obj_create(parent);
  lv_obj_set_size(ctx->root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(ctx->root, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(ctx->root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(ctx->root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(ctx->root, 30, 0);
  lv_obj_set_style_pad_hor(ctx->root, 20, 0);

  ctx->img_obj = lv_image_create(ctx->root);
  lv_obj_set_size(ctx->img_obj, PREVIEW_W, PREVIEW_H);
  lv_image_set_scale(ctx->img_obj, SCALE_FACTOR);
  lv_image_set_src(ctx->img_obj, &ctx->img_dsc);
  lv_obj_set_style_radius(ctx->img_obj, 12, 0);
  lv_obj_set_style_clip_corner(ctx->img_obj, true, 0);

  lv_obj_t *hint = lv_label_create(ctx->root);
  lv_label_set_text(hint, "人脸检测预览");
  lv_obj_set_style_text_color(hint, S_TEXT_SECONDARY, 0);
  lv_obj_set_style_margin_top(hint, 8, 0);

  return ctx->root;
}

/* 周期刷新回调（替代原 Task 轮询，由 LVGL 线程带起） */
static void fd_preview_update(void *ctx_in) {
  fd_preview_ctx_t *ctx = (fd_preview_ctx_t *)ctx_in;
  if (!ctx || !ctx->img_obj)
    return;

  // 从共享缓存中尝试提取已画框的 RGB565 帧
  if (face_detector_helper_get_latest_rgb565(ctx->fb_buf, CAM_W, CAM_H)) {
    if (lvgl_port_lock(0)) {
      lv_image_set_src(ctx->img_obj, &ctx->img_dsc);
      lv_obj_invalidate(ctx->img_obj);
      lvgl_port_unlock();
    }
  }
}

/* 反初始化回调 */
static void fd_preview_deinit(void *ctx_in) {
  fd_preview_ctx_t *ctx = (fd_preview_ctx_t *)ctx_in;
  if (!ctx)
    return;

  // 停止人脸检测（自动退订 cam_helper，若无其它订阅则自动下电摄像头）
  face_detector_helper_stop_continuous();

  if (ctx->fb_buf) {
    heap_caps_free(ctx->fb_buf);
    ctx->fb_buf = NULL;
  }

  free(ctx);
  ESP_LOGI(TAG, "Face detection preview deinitialized");
}

const gs_page_desc_t page_fd = {
    .init_cb = fd_preview_init,
    .render_cb = fd_preview_render,
    .update_cb = fd_preview_update,
    .deinit_cb = fd_preview_deinit,
};

/* ─────────────────────────────────────────────────────────────
 * 2. 自定义 VFS 文件浏览器
 * ───────────────────────────────────────────────────────────── */
#define VFS_ROOT_PATH "/littlefs"

typedef struct {
  lv_obj_t *root;
  lv_obj_t *list;
  lv_obj_t *path_lbl;
  char current_path[128];
} vfs_explorer_ctx_t;

static void populate_file_list(vfs_explorer_ctx_t *ctx);

static void on_item_click(lv_event_t *e) {
  vfs_explorer_ctx_t *ctx = (vfs_explorer_ctx_t *)lv_event_get_user_data(e);
  lv_obj_t *btn = lv_event_get_target(e);
  const char *name = (const char *)lv_obj_get_user_data(btn);
  if (!name || !ctx)
    return;

  if (strcmp(name, "..") == 0) {
    char *last_slash = strrchr(ctx->current_path, '/');
    if (last_slash && last_slash != ctx->current_path) {
      *last_slash = '\0';
    } else {
      strcpy(ctx->current_path, VFS_ROOT_PATH);
    }
  } else {
    // 缓冲区调大到 384 字节，满足 ctx->current_path(128) + '/' + name(255)
    char next_path[384];
    snprintf(next_path, sizeof(next_path), "%.128s/%.255s", ctx->current_path,
             name);

    struct stat st;
    if (stat(next_path, &st) == 0 && S_ISDIR(st.st_mode)) {
      strncpy(ctx->current_path, next_path, sizeof(ctx->current_path) - 1);
      ctx->current_path[sizeof(ctx->current_path) - 1] = '\0';
    }
  }
  populate_file_list(ctx);
}

static void populate_file_list(vfs_explorer_ctx_t *ctx) {
  if (!ctx || !ctx->list)
    return;

  lv_obj_clean(ctx->list);
  lv_label_set_text(ctx->path_lbl, ctx->current_path);

  if (strcmp(ctx->current_path, VFS_ROOT_PATH) != 0) {
    lv_obj_t *btn =
        lv_list_add_button(ctx->list, LV_SYMBOL_LEFT, ".. (返回上级)");
    lv_obj_set_user_data(btn, (void *)"..");
    lv_obj_set_style_text_color(btn, S_COLOR_PRIMARY, 0);
    lv_obj_add_event_cb(btn, on_item_click, LV_EVENT_CLICKED, ctx);
  }

  DIR *dir = opendir(ctx->current_path);
  if (!dir) {
    lv_obj_t *btn =
        lv_list_add_button(ctx->list, LV_SYMBOL_WARNING, "无法打开目录");
    lv_obj_set_style_text_color(btn, S_TEXT_SECONDARY, 0);
    return;
  }

  struct dirent *entry;
  int count = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    // 扩大全路径缓冲区至 384 字节 (128 + 1 + 255)
    char full_path[384];
    snprintf(full_path, sizeof(full_path), "%.128s/%.255s", ctx->current_path,
             entry->d_name);

    struct stat st;
    bool is_dir = false;
    size_t fsize = 0;
    if (stat(full_path, &st) == 0) {
      is_dir = S_ISDIR(st.st_mode);
      fsize = st.st_size;
    }

    // 扩大显示文字缓冲区至 300 字节 (255 + 额外字符)
    char item_text[300];
    const char *icon = is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;

    if (is_dir) {
      snprintf(item_text, sizeof(item_text), "%.255s/", entry->d_name);
    } else {
      if (fsize < 1024) {
        snprintf(item_text, sizeof(item_text), "%.255s (%u B)", entry->d_name,
                 (unsigned int)fsize);
      } else {
        snprintf(item_text, sizeof(item_text), "%.255s (%u KB)", entry->d_name,
                 (unsigned int)(fsize / 1024));
      }
    }

    lv_obj_t *btn = lv_list_add_button(ctx->list, icon, item_text);
    char *name_copy = strdup(entry->d_name);
    lv_obj_set_user_data(btn, name_copy);

    if (is_dir) {
      lv_obj_set_style_text_color(btn, S_TEXT_PRIMARY, 0);
      lv_obj_add_event_cb(btn, on_item_click, LV_EVENT_CLICKED, ctx);
    } else {
      lv_obj_set_style_text_color(btn, S_TEXT_SECONDARY, 0);
    }
    count++;
  }
  closedir(dir);

  if (count == 0) {
    lv_obj_t *btn =
        lv_list_add_button(ctx->list, LV_SYMBOL_WARNING, "目录为空");
    lv_obj_set_style_text_color(btn, S_TEXT_SECONDARY, 0);
  }
}

static void *fe_init(void *args) {
  vfs_explorer_ctx_t *ctx = (vfs_explorer_ctx_t *)calloc(1, sizeof(*ctx));
  if (!ctx)
    return NULL;
  strcpy(ctx->current_path, VFS_ROOT_PATH);
  return ctx;
}

static lv_obj_t *fe_render(lv_obj_t *parent, void *ctx_in) {
  vfs_explorer_ctx_t *ctx = (vfs_explorer_ctx_t *)ctx_in;

  ctx->root = lv_obj_create(parent);
  lv_obj_set_size(ctx->root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(ctx->root, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(ctx->root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(ctx->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_pad_top(ctx->root, 36, 0);
  lv_obj_set_style_pad_bottom(ctx->root, 30, 0);
  lv_obj_set_style_pad_hor(ctx->root, 24, 0);

  ctx->path_lbl = lv_label_create(ctx->root);
  lv_label_set_text(ctx->path_lbl, ctx->current_path);
  lv_obj_set_style_text_font(ctx->path_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ctx->path_lbl, S_COLOR_PRIMARY, 0);
  lv_obj_set_style_margin_bottom(ctx->path_lbl, 8, 0);

  ctx->list = lv_list_create(ctx->root);
  lv_obj_set_size(ctx->list, LV_PCT(100), LV_PCT(80));
  lv_obj_set_style_bg_color(ctx->list, S_BG_CARD, 0);
  lv_obj_set_style_radius(ctx->list, S_RADIUS_CARD, 0);
  lv_obj_set_style_border_width(ctx->list, 0, 0);

  populate_file_list(ctx);

  return ctx->root;
}

static void fe_deinit(void *ctx_in) {
  vfs_explorer_ctx_t *ctx = (vfs_explorer_ctx_t *)ctx_in;
  if (ctx) {
    free(ctx);
  }
}

static const gs_page_desc_t page_fexplorer = {
    .init_cb = fe_init, .render_cb = fe_render, .deinit_cb = fe_deinit};

/* ─────────────────────────────────────────────────────────────
 * 3. 调试主界面
 * ───────────────────────────────────────────────────────────── */
extern const gs_page_desc_t page_cam_test;

static void on_btn_click(lv_event_t *e) {
  const char *action = (const char *)lv_event_get_user_data(e);
  if (!action)
    return;

  if (strcmp(action, "face") == 0) {
    gs_nav_push(&page_fd, NULL);
  } else if (strcmp(action, "files") == 0) {
    gs_nav_push(&page_fexplorer, NULL);
  } else if (strcmp(action, "cam") == 0) {
    gs_nav_push(&page_cam_test, NULL);
  } else if (strcmp(action, "exit") == 0) {
    gs_nav_pop();
  }
}

static lv_obj_t *make_debug_btn(lv_obj_t *parent, const char *icon,
                                const char *label, const char *action) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_width(btn, LV_PCT(85));
  lv_obj_set_style_radius(btn, S_RADIUS_CARD, 0);
  lv_obj_set_style_bg_color(btn, S_BG_CARD, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_pad_ver(btn, 12, 0);
  lv_obj_set_style_pad_hor(btn, S_PAD_H, 0);

  lv_obj_t *row = lv_obj_create(btn);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);

  lv_obj_t *icon_lbl = lv_label_create(row);
  lv_label_set_text(icon_lbl, icon);
  lv_obj_set_style_text_color(icon_lbl, S_COLOR_PRIMARY, 0);
  lv_obj_set_style_margin_right(icon_lbl, 10, 0);
  lv_obj_set_style_text_font(icon_lbl, LV_FONT_DEFAULT, 0);

  lv_obj_t *text_lbl = lv_label_create(row);
  lv_label_set_text(text_lbl, label);
  lv_obj_set_style_text_color(text_lbl, S_TEXT_PRIMARY, 0);
  lv_obj_set_flex_grow(text_lbl, 1);

  lv_obj_t *arrow = lv_label_create(row);
  lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(arrow, S_TEXT_SECONDARY, 0);
  lv_obj_set_style_text_font(arrow, LV_FONT_DEFAULT, 0);

  if (action) {
    lv_obj_add_event_cb(btn, on_btn_click, LV_EVENT_CLICKED, (void *)action);
  }
  return btn;
}

static void *debug_init(void *args) {
  monitor_task_pause();
  ESP_LOGI(TAG, "调试模式：监控已暂停");
  static int sentinel;
  return &sentinel;
}

static lv_obj_t *debug_render(lv_obj_t *parent, void *ctx) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_pad_ver(cont, 30, 0);
  lv_obj_set_style_pad_hor(cont, 20, 0);
  lv_obj_set_style_pad_gap(cont, S_GAP, 0);

  lv_obj_t *title = lv_label_create(cont);
  lv_label_set_text(title, "Debug Mode");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, S_TEXT_PRIMARY, 0);
  lv_obj_set_style_margin_bottom(title, 2, 0);

  make_debug_btn(cont, LV_SYMBOL_IMAGE, "人脸检测预览", "face");
  make_debug_btn(cont, LV_SYMBOL_IMAGE, "摄像头测试", "cam");
  make_debug_btn(cont, LV_SYMBOL_DIRECTORY, "文件管理", "files");
  make_debug_btn(cont, LV_SYMBOL_CLOSE, "退出调试", "exit");

  lv_obj_t *info = lv_label_create(cont);
  lv_label_set_text(info, "后台任务已暂停");
  lv_obj_set_style_text_color(info, S_TEXT_SECONDARY, 0);
  lv_obj_set_style_margin_top(info, 4, 0);

  return cont;
}

static void debug_deinit(void *ctx) {
  monitor_task_resume();
  ESP_LOGI(TAG, "调试退出：监控已恢复");
}

const gs_page_desc_t page_debug = {
    .init_cb = debug_init,
    .render_cb = debug_render,
    .deinit_cb = debug_deinit,
};