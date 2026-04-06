# 测试方式
```c
#ifndef __TIME_TEST_HELPER__
#define __TIME_TEST_HELPER__

#include "esp_timer.h"

#define TEST_TIME(func, ...)                                                   \
  do {                                                                         \
    int64_t _start = esp_timer_get_time();                                     \
    func(__VA_ARGS__);                                                         \
    int64_t _end = esp_timer_get_time();                                       \
    printf("%s took %lld us\n", #func, _end - _start);                         \
  } while (0)

#endif // !__TIME_TEST_HELPER__
```

## jpeg 解码与编码到显示的速度(esp new jpeg)
### YUV422->YUV422(60%,422)->RGB565BE
jpeg_encode_decode_once took 49494 us
### YUV422->YUV422(60%,420)->RGB565BE
jpeg_encode_decode_once took 38199 us
### YUV422->YUV422(40%,420)->RGB565BE
jpeg_encode_decode_once took 35287 us
### YUV422->YUV422(50%,420)->RGB565BE
jpeg_encode_decode_once took 35881 us

### RGB565->RGB565(60%,422)->RGB565BE
jpeg_encode_decode_once took 61903 us
### RGB565->RGB565(60%,420)->RGB565BE
jpeg_encode_decode_once took 52532 us

## YUV422->RGB565(BE)
### 未使用加速
test_yuv422_display_once took 18839 us
```c
#define CLAMP(x) ((x) > 255 ? 255 : ((x) < 0 ? 0 : (x)))

uint8_t *yuv422_to_rgb565(const uint8_t *in_buf, int width, int height,
                          size_t *out_size) {
  *out_size = width * height * 2;
  uint16_t *out_buf = (uint16_t *)heap_caps_malloc(
      *out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

  if (!out_buf)
    return NULL;

  int y0, u, y1, v;
  int r, g, b;
  int c, d, e;

  for (int i = 0, j = 0; i < width * height; i += 2, j += 4) {
    y0 = in_buf[j];
    u = in_buf[j + 1];
    y1 = in_buf[j + 2];
    v = in_buf[j + 3];

    d = u - 128;
    e = v - 128;

    c = 1164 * (y0 - 16);
    r = CLAMP((c + 1596 * e) >> 10);
    g = CLAMP((c - 391 * d - 813 * e) >> 10);
    b = CLAMP((c + 2018 * d) >> 10);
    uint16_t pixel1 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    out_buf[i] = (pixel1 << 8) | (pixel1 >> 8);

    c = 1164 * (y1 - 16);
    r = CLAMP((c + 1596 * e) >> 10);
    g = CLAMP((c - 391 * d - 813 * e) >> 10);
    b = CLAMP((c + 2018 * d) >> 10);
    uint16_t pixel2 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    out_buf[i + 1] = (pixel2 << 8) | (pixel2 >> 8);
  }

  return (uint8_t *)out_buf;
}
```
### 使用 SIMD 加速
test_yuv422_display_once took 47491 us
```c
#include "vector.h"
#include <math.h>

#include <sys/param.h>

#define CHUNK_SIZE 64

uint8_t *yuv422_to_rgb565_simd(const uint8_t *in_buf, int width, int height,
                               size_t *out_size) {
  *out_size = width * height * 2;
  uint16_t *out_buf = (uint16_t *)heap_caps_aligned_alloc(
      16, *out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!out_buf)
    return NULL;

  int total_pixels = width * height;

  VECTOR_STACK_INIT(vec_y, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_u, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_v, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_r, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_g, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_b, CHUNK_SIZE, DTYPE_INT32);
  VECTOR_STACK_INIT(vec_tmp, CHUNK_SIZE, DTYPE_INT32);

  int32_t *y_data = (int32_t *)vec_y.data;
  int32_t *u_data = (int32_t *)vec_u.data;
  int32_t *v_data = (int32_t *)vec_v.data;
  int32_t *r_ptr = (int32_t *)vec_r.data;
  int32_t *g_ptr = (int32_t *)vec_g.data;
  int32_t *b_ptr = (int32_t *)vec_b.data;

  for (int p = 0; p < total_pixels; p += CHUNK_SIZE) {
    // 数据解交织
    for (int i = 0; i < CHUNK_SIZE; i += 2) {
      int in_idx = (p + i) * 2;
      y_data[i] = in_buf[in_idx];
      y_data[i + 1] = in_buf[in_idx + 2];
      u_data[i] = u_data[i + 1] = (int32_t)in_buf[in_idx + 1] - 128;
      v_data[i] = v_data[i + 1] = (int32_t)in_buf[in_idx + 3] - 128;
    }

    // SIMD 运算
    vec_add_scalar(&vec_y, -16, &vec_y);
    vec_mul_scalar(&vec_y, 1164, &vec_y, 0);

    vec_mul_scalar(&vec_v, 1596, &vec_r, 0);
    vec_add(&vec_y, &vec_r, &vec_r);
    vec_mul_scalar(&vec_r, 1, &vec_r, 10);

    vec_mul_scalar(&vec_u, 2018, &vec_b, 0);
    vec_add(&vec_y, &vec_b, &vec_b);
    vec_mul_scalar(&vec_b, 1, &vec_b, 10);

    vec_mul_scalar(&vec_u, -391, &vec_g, 0);
    vec_mul_scalar(&vec_v, -813, &vec_tmp, 0);
    vec_add(&vec_g, &vec_tmp, &vec_g);
    vec_add(&vec_y, &vec_g, &vec_g);
    vec_mul_scalar(&vec_g, 1, &vec_g, 10);

    // 封装输出
    for (int i = 0; i < CHUNK_SIZE; i++) {
      int32_t r = MAX(0, MIN(255, r_ptr[i]));
      int32_t g = MAX(0, MIN(255, g_ptr[i]));
      int32_t b = MAX(0, MIN(255, b_ptr[i]));

      uint16_t pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      out_buf[p + i] = (pixel << 8) | (pixel >> 8);
    }
  }
  return (uint8_t *)out_buf;
}
```