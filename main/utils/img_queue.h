#ifndef IMG_QUEUE_H
#define IMG_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  IMG_TYPE_MONITOR = 0, // 自动监控定时检测
  IMG_TYPE_MANUAL = 1,  // 手动拍照任务完成
} img_job_type_t;

/**
 * @brief 上传优先级
 */
typedef enum {
  IMG_PRIORITY_LOW = 0,  // 自动监控任务
  IMG_PRIORITY_HIGH = 1, // 手动拍照任务
} img_priority_t;

/**
 * @brief 上传结果回调函数类型
 * @param success   是否上传成功
 * @param image_key 成功时返回 S3 中的 key，失败时为 NULL
 * @param user_data 用户自定义数据
 */
typedef void (*img_upload_callback_t)(bool success, const char *image_key,
                                      void *user_data);

/**
 * @brief 上传任务描述
 */
typedef struct {
  char path[128];          // 本地文件路径
  int task_id;             // 关联的任务 ID，0 表示无关联
  img_priority_t priority; // 优先级
  uint8_t retry_count;     // 当前重试次数（内部使用）
  img_job_type_t type;
  // 回调与上下文
  img_upload_callback_t on_complete; // 完成回调（成功或最终失败后调用）
  void *user_data;                   // 传递给回调的用户数据

  // 内部状态（由上传器填充）
  char image_key[128]; // 上传成功后服务器返回的 key
} img_job_t;

/**
 * @brief 初始化上传队列
 */
void img_queue_init(void);

/**
 * @brief 将任务加入队列（线程安全）
 * @param job 任务描述，会被拷贝到内部队列
 * @return true 成功入队，false 队列已满
 */
bool img_queue_push(const img_job_t *job);

/**
 * @brief 从队列中取出优先级最高且未超限的任务（不移除）
 * @param out_job 输出任务副本
 * @return true 成功获取，false 队列空
 */
bool img_queue_peek(img_job_t *out_job);

/**
 * @brief 移除队列头部任务（必须在 peek 成功且处理完后调用）
 * @return true 成功移除，false 队列空或未先 peeking
 */
bool img_queue_commit(void);

/**
 * @brief 更新当前头部任务的重试次数
 * @param new_retry_count 新的重试次数
 */
void img_queue_update_retry(uint8_t new_retry_count);

/**
 * @brief 获取队列中当前任务数量
 */
int img_queue_get_count(void);

/**
 * @brief 检查队列是否已满
 */
bool img_queue_is_full(void);

#ifdef __cplusplus
}
#endif

#endif