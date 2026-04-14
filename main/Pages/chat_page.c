// page_chat.c
#include "chat_comp.h"
#include "chat_service.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "net_adapter.h"
#include "protocol.h"

#include "esp_heap_caps.h"

static const char *TAG = "GS_CHAT_PAGE";

typedef struct {
  bool is_active;
} chat_page_ctx_t;

// 聊天页面自定义网络状态回调
static void chat_status_callback(net_status_t status, const char *msg) {
  switch (status) {
  case NET_STATUS_CONNECTED:
    gs_toast_show(msg, GS_TOAST_SUCCESS);
    break;
  case NET_STATUS_DISCONNECTED:
    gs_toast_show(msg, GS_TOAST_FAILED);
    break;
  case NET_STATUS_RECONNECTING:
    gs_toast_show(msg, GS_TOAST_INFO);
    break;
  case NET_STATUS_CONNECTING:
    // 可以忽略或显示“连接中”
    break;
  default:
    break;
  }
}

static void *chat_init_cb(void *args) {
  ESP_LOGI("CHAT", "Initialising chat context...");

  chat_page_ctx_t *ctx = heap_caps_calloc(1, sizeof(chat_page_ctx_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (ctx == NULL) {
    ESP_LOGE(
        "CHAT",
        "CRITICAL: Failed to allocate context! Free Internal: %d, PSRAM: %d",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return NULL;
  }

  net_set_status_callback(chat_status_callback);

  chat_enter_live();

  ESP_LOGI("CHAT", "Context allocated at %p", ctx); // 打印指针地址确认分配成功
  return ctx;
}

static lv_obj_t *chat_render_cb(lv_obj_t *parent, void *ctx) {
  return chat_comp_create(parent);
}

static void chat_update_cb(void *ctx) {
  chat_comp_loop(); // 处理接收数据及UI更新
}

static void chat_deinit_cb(void *ctx) {
  chat_exit_chat();
  // 恢复默认网络状态回调（Toast）
  net_reset_status_callback();

  free(ctx);
  ESP_LOGI(TAG, "Chat page destroyed, switched to HOME mode.");
}

const gs_page_desc_t page_chat = {.init_cb = chat_init_cb,
                                  .render_cb = chat_render_cb,
                                  .update_cb = chat_update_cb,
                                  .deinit_cb = chat_deinit_cb};