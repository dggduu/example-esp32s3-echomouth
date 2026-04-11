// page_chat.c
#include "chat_comp.h"
#include "chat_service.h"
#include "esp_log.h"
#include "gs_nav.h"
#include "gs_portal.h"
#include "net_adapter.h"
#include "protocol.h"

static const char *TAG = "GS_CHAT_PAGE";

typedef struct {
  // 可保留一些页面状态，如网络配置（不再管理连接）
} chat_page_ctx_t;

// 聊天页面自定义网络状态回调
static void chat_status_callback(net_status_t status, const char *msg) {
  switch (status) {
  case NET_STATUS_CONNECTED:
    // 重连成功后刷新聊天窗口
    chat_enter_live();
    gs_toast_show("聊天已连接", GS_TOAST_SUCCESS);
    break;
  case NET_STATUS_DISCONNECTED:
    gs_toast_show("聊天连接断开", GS_TOAST_FAILED);
    break;
  case NET_STATUS_RECONNECTING:
    gs_toast_show("正在重连聊天...", GS_TOAST_INFO);
    break;
  }
}

static void *chat_init_cb(void *args) {
  chat_page_ctx_t *ctx = calloc(1, sizeof(chat_page_ctx_t));
  if (!ctx)
    return NULL;

  // 设置页面专用网络状态回调
  net_set_status_callback(chat_status_callback);

  // 切换至聊天模式
  if (protocol_get_mode() != DEVICE_MODE_CHAT_LIVE) {
    protocol_send_mode_switch(DEVICE_MODE_CHAT_LIVE);
    protocol_switch_mode(DEVICE_MODE_CHAT_LIVE);
  }

  // 初始化聊天服务并请求最新消息
  chat_enter_live();

  return ctx;
}

static lv_obj_t *chat_render_cb(lv_obj_t *parent, void *ctx) {
  return chat_comp_create(parent);
}

static void chat_update_cb(void *ctx) {
  chat_comp_loop(); // 处理接收数据及UI更新
}

static void chat_deinit_cb(void *ctx) {
  // 退出聊天页面，切换回 HOME 模式
  if (protocol_get_mode() != DEVICE_MODE_HOME) {
    protocol_send_mode_switch(DEVICE_MODE_HOME);
    protocol_switch_mode(DEVICE_MODE_HOME);
  }

  // 恢复默认网络状态回调（Toast）
  net_reset_status_callback();

  free(ctx);
  ESP_LOGI(TAG, "Chat page destroyed, switched to HOME mode.");
}

const gs_page_desc_t page_chat = {.init_cb = chat_init_cb,
                                  .render_cb = chat_render_cb,
                                  .update_cb = chat_update_cb,
                                  .deinit_cb = chat_deinit_cb};