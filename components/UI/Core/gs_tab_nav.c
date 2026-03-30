#include "esp_log.h"
#include "gs_nav.h"


static const char *TAG = "gs_tab_nav";

static struct {
  lv_obj_t *main_cont; // 水平滚动的父容器
  uint32_t page_count;
} s_tab_nav_mgr = {0};

void gs_tab_nav_init(lv_obj_t *parent) {
  if (!parent)
    return;

  // 1. 创建水平滚动容器
  s_tab_nav_mgr.main_cont = lv_obj_create(parent);
  s_tab_nav_mgr.page_count = 0;

  lv_obj_set_size(s_tab_nav_mgr.main_cont, LV_PCT(100), LV_PCT(100));

  // 2. 设置布局为水平 Flex 且不换行
  lv_obj_set_flex_flow(s_tab_nav_mgr.main_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_tab_nav_mgr.main_cont, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // 3. 关键：设置滚动吸附和单页滑动限制
  lv_obj_set_scroll_snap_x(s_tab_nav_mgr.main_cont, LV_SCROLL_SNAP_CENTER);
  lv_obj_add_flag(s_tab_nav_mgr.main_cont, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_set_scrollbar_mode(s_tab_nav_mgr.main_cont, LV_SCROLLBAR_MODE_OFF);

  // 移除默认边距和边框
  lv_obj_set_style_pad_all(s_tab_nav_mgr.main_cont, 0, 0);
  lv_obj_set_style_pad_column(s_tab_nav_mgr.main_cont, 0, 0);
  lv_obj_set_style_border_width(s_tab_nav_mgr.main_cont, 0, 0);
  lv_obj_set_style_radius(s_tab_nav_mgr.main_cont, 0, 0);
}

int gs_tab_nav_add_page(const gs_page_desc_t *page, void *args) {
  if (!s_tab_nav_mgr.main_cont || !page) {
    ESP_LOGE(TAG, "Nav not initialized or invalid page desc");
    return -1;
  }

  // 1. 初始化页面私有数据
  void *ctx = page->init_cb ? page->init_cb(args) : NULL;

  // 2. 在 main_cont 中渲染页面
  lv_obj_t *page_obj = page->render_cb(s_tab_nav_mgr.main_cont, ctx);

  if (page_obj) {
    // 强制设置页面为全屏尺寸
    lv_obj_set_size(page_obj, LV_PCT(100), LV_PCT(100));
    // 确保页面参与吸附
    lv_obj_add_flag(page_obj, LV_OBJ_FLAG_SNAPPABLE);

    s_tab_nav_mgr.page_count++;
    ESP_LOGI(TAG, "Page added, total: %d", (int)s_tab_nav_mgr.page_count);
    return 0;
  }

  return -1;
}

void gs_tab_nav_set_index(uint32_t index, lv_anim_enable_t anim) {
  if (index >= s_tab_nav_mgr.page_count)
    return;

  lv_obj_t *page = lv_obj_get_child(s_tab_nav_mgr.main_cont, index);
  if (page) {
    lv_obj_scroll_to_view(page, anim);
  }
}