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
### YUV422->YUV422(50%,420)->RGB565BE（目前使用的）
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
### 使用 SIMD 加速（目前使用的，实际使用时还进行了下采样，下面的函数没有进行下采样，为了统一标准）
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

## 模型测试日志
```js
Starting ESP-DL Model Test...
I (849) FbsLoader: The storage free size is 24576 KB
I (849) FbsLoader: The partition size is 1024 KB

I (1139) dl::Model: Testing output raw_conf.
I (1139) dl::Model: Testing output raw_loc.
I (1149) dl::Model: Test Pass!
I (1149) DL_TEST: Model test passed! Architecture is compatible with S3 PIE.
I (1149) dl::Model: ------------------------------- 0 -------------------------------
I (1159) Conv: pads: [1, 1, 1, 1], strides: [2, 2], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1169) dl::Model: ------------------------------- 1 -------------------------------
I (1169) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 16, activation: ReLU, quant_type: symm 8bit.
I (1189) dl::Model: ------------------------------- 2 -------------------------------
I (1189) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1199) dl::Model: ------------------------------- 3 -------------------------------
I (1209) Conv: pads: [1, 1, 1, 1], strides: [2, 2], dilations: [1, 1], group: 32, activation: ReLU, quant_type: symm 8bit.
I (1219) dl::Model: ------------------------------- 4 -------------------------------
I (1229) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1239) dl::Model: ------------------------------- 5 -------------------------------
I (1249) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 32, activation: ReLU, quant_type: symm 8bit.
I (1259) dl::Model: ------------------------------- 6 -------------------------------
I (1269) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1279) dl::Model: ------------------------------- 7 -------------------------------
I (1279) Conv: pads: [1, 1, 1, 1], strides: [2, 2], dilations: [1, 1], group: 32, activation: ReLU, quant_type: symm 8bit.
I (1299) dl::Model: ------------------------------- 8 -------------------------------
I (1299) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1309) dl::Model: ------------------------------- 9 -------------------------------
I (1319) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 64, activation: ReLU, quant_type: symm 8bit.
I (1329) dl::Model: ------------------------------- 10 -------------------------------
I (1339) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1349) dl::Model: ------------------------------- 11 -------------------------------
I (1359) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 64, activation: ReLU, quant_type: symm 8bit.
I (1369) dl::Model: ------------------------------- 12 -------------------------------
I (1379) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1389) dl::Model: ------------------------------- 13 -------------------------------
I (1389) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 64, activation: ReLU, quant_type: symm 8bit.
I (1409) dl::Model: ------------------------------- 14 -------------------------------
I (1409) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1419) dl::Model: ------------------------------- 15 -------------------------------
I (1429) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 64, activation: ReLU, quant_type: symm 8bit.
I (1439) dl::Model: ------------------------------- 16 -------------------------------
I (1449) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 64, activation: ReLU, quant_type: symm 8bit.
I (1459) dl::Model: ------------------------------- 17 -------------------------------
I (1469) Conv: pads: [1, 1, 1, 1], strides: [2, 2], dilations: [1, 1], group: 64, activation: ReLU, quant_type: symm 8bit.
I (1479) dl::Model: ------------------------------- 18 -------------------------------
I (1489) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (1499) dl::Model: ------------------------------- 19 -------------------------------
I (1499) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (1519) dl::Model: ------------------------------- 20 -------------------------------
I (1519) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1529) dl::Model: ------------------------------- 21 -------------------------------
I (1539) Reshape: quant_type: symm 8bit, shape: [3].
I (1549) dl::Model: ------------------------------- 22 -------------------------------
I (1549) Reshape: quant_type: symm 8bit, shape: [3].
I (1559) dl::Model: ------------------------------- 23 -------------------------------
I (1569) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 128, activation: ReLU, quant_type: symm 8bit.
I (1579) dl::Model: ------------------------------- 24 -------------------------------
I (1579) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1589) dl::Model: ------------------------------- 25 -------------------------------
I (1599) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 128, activation: ReLU, quant_type: symm 8bit.
I (1609) dl::Model: ------------------------------- 26 -------------------------------
I (1619) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1629) dl::Model: ------------------------------- 27 -------------------------------
I (1639) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 128, activation: ReLU, quant_type: symm 8bit.
I (1649) dl::Model: ------------------------------- 28 -------------------------------
I (1659) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 128, activation: ReLU, quant_type: symm 8bit.
I (1669) dl::Model: ------------------------------- 29 -------------------------------
I (1679) Conv: pads: [1, 1, 1, 1], strides: [2, 2], dilations: [1, 1], group: 128, activation: ReLU, quant_type: symm 8bit.
I (1689) dl::Model: ------------------------------- 30 -------------------------------
I (1689) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (1709) dl::Model: ------------------------------- 31 -------------------------------
I (1709) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (1719) dl::Model: ------------------------------- 32 -------------------------------
I (1729) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1739) dl::Model: ------------------------------- 33 -------------------------------
I (1749) Reshape: quant_type: symm 8bit, shape: [3].
I (1749) dl::Model: ------------------------------- 34 -------------------------------
I (1759) Reshape: quant_type: symm 8bit, shape: [3].
I (1769) dl::Model: ------------------------------- 35 -------------------------------
I (1769) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 256, activation: ReLU, quant_type: symm 8bit.
I (1789) dl::Model: ------------------------------- 36 -------------------------------
I (1789) RequantizeLinear: quant_type: symm 8bit.
I (1799) dl::Model: ------------------------------- 37 -------------------------------
I (1799) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1819) dl::Model: ------------------------------- 38 -------------------------------
I (1819) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 256, activation: ReLU, quant_type: symm 8bit.
I (1829) dl::Model: ------------------------------- 39 -------------------------------
I (1839) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 256, activation: ReLU, quant_type: symm 8bit.
I (1849) dl::Model: ------------------------------- 40 -------------------------------
I (1859) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1869) dl::Model: ------------------------------- 41 -------------------------------
I (1879) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (1889) dl::Model: ------------------------------- 42 -------------------------------
I (1899) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (1909) dl::Model: ------------------------------- 43 -------------------------------
I (1919) Conv: pads: [1, 1, 1, 1], strides: [2, 2], dilations: [1, 1], group: 64, activation: ReLU, quant_type: symm 8bit.
I (1929) dl::Model: ------------------------------- 44 -------------------------------
I (1929) Reshape: quant_type: symm 8bit, shape: [3].
I (1939) dl::Model: ------------------------------- 45 -------------------------------
I (1949) Reshape: quant_type: symm 8bit, shape: [3].
I (1949) dl::Model: ------------------------------- 46 -------------------------------
I (1959) Conv: pads: [0, 0, 0, 0], strides: [1, 1], dilations: [1, 1], group: 1, activation: ReLU, quant_type: symm 8bit.
I (1969) dl::Model: ------------------------------- 47 -------------------------------
I (1979) RequantizeLinear: quant_type: symm 8bit.
I (1979) dl::Model: ------------------------------- 48 -------------------------------
I (1989) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (1999) dl::Model: ------------------------------- 49 -------------------------------
I (2009) Conv: pads: [1, 1, 1, 1], strides: [1, 1], dilations: [1, 1], group: 1, activation: None, quant_type: symm 8bit.
I (2019) dl::Model: ------------------------------- 50 -------------------------------
I (2029) Reshape: quant_type: symm 8bit, shape: [3].
I (2029) dl::Model: ------------------------------- 51 -------------------------------
I (2039) Reshape: quant_type: symm 8bit, shape: [3].
I (2039) dl::Model: ------------------------------- 52 -------------------------------
I (2049) Concat: quant_type: symm 8bit.
I (2049) dl::Model: ------------------------------- 53 -------------------------------
I (2059) RequantizeLinear: quant_type: symm 8bit.
I (2069) dl::Model: ------------------------------- 54 -------------------------------
I (2069) Concat: quant_type: symm 8bit.
I (2079) dl::Model: -------------------------------------------------------------

I (2089) main_task: Returned from app_main()
```


### 320x240（第一版）
```js
I (2469) dl::Model: model:main_graph, version:0
I (2479) dl::Model: MODEL LOCATION IN FLASH PARTITION
I (2479) dl::Model: +----------------+--------------+-----------+--------------+
I (2489) dl::Model: |                      memory summary                      |
I (2499) dl::Model: +----------------+--------------+-----------+--------------+
I (2499) dl::Model: |                | internal RAM | PSRAM     | FLASH        |
I (2509) dl::Model: +----------------+--------------+-----------+--------------+
I (2519) dl::Model: | fbs_model      |              |           | 418.22KB     |
I (2529) dl::Model: |  -- parameter  |              |           |  -- 262.53KB |
I (2529) dl::Model: +----------------+--------------+-----------+--------------+
I (2539) dl::Model: | parameter_copy |              | 262.53KB  |              |
I (2549) dl::Model: +----------------+--------------+-----------+--------------+
I (2549) dl::Model: | variable       |              | 1275.00KB |              |
I (2559) dl::Model: +----------------+--------------+-----------+--------------+
I (2569) dl::Model: | others         | 30.20KB      | 10.79KB   |              |
I (2579) dl::Model: +----------------+--------------+-----------+--------------+
I (2579) dl::Model: | total          | 30.20KB      | 1548.32KB | 418.22KB     |
I (2589) dl::Model: +----------------+--------------+-----------+--------------+

I (2769) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2769) dl::Model: |                                     module summary                                      |
I (2769) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2779) dl::Model: | name                                                      | type             | latency  |
I (2789) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2809) dl::Model: | total                                                     |                  | 160739us |
I (2809) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2819) dl::Model: | /base_net.0/base_net.0.0/Conv                             | Conv             | 28814us  |
I (2829) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2839) dl::Model: | /base_net.1/base_net.1.2/Conv                             | Conv             | 20463us  |
I (2849) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2859) dl::Model: | /base_net.1/base_net.1.0/Conv                             | Conv             | 12445us  |
I (2869) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2879) dl::Model: | /base_net.2/base_net.2.0/Conv                             | Conv             | 11242us  |
I (2889) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2899) dl::Model: | /base_net.12/base_net.12.2/Conv                           | Conv             | 10511us  |
I (2909) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2919) dl::Model: | /base_net.3/base_net.3.0/Conv                             | Conv             | 5965us   |
I (2929) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2939) dl::Model: | /base_net.2/base_net.2.2/Conv                             | Conv             | 5569us   |
I (2949) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2959) dl::Model: | /base_net.3/base_net.3.2/Conv                             | Conv             | 5557us   |
I (2969) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2979) dl::Model: | /regression_headers.0/regression_headers.0.2/Conv         | Conv             | 5177us   |
I (2989) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2999) dl::Model: | /base_net.7/base_net.7.2/Conv                             | Conv             | 3285us   |
I (3009) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3019) dl::Model: | /base_net.6/base_net.6.2/Conv                             | Conv             | 3258us   |
I (3029) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3039) dl::Model: | /base_net.5/base_net.5.2/Conv                             | Conv             | 3252us   |
I (3049) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3059) dl::Model: | /classification_headers.0/classification_headers.0.2/Conv | Conv             | 3013us   |
I (3069) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3079) dl::Model: | /base_net.4/base_net.4.0/Conv                             | Conv             | 2903us   |
I (3089) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3099) dl::Model: | /base_net.5/base_net.5.0/Conv                             | Conv             | 2673us   |
I (3109) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3119) dl::Model: | /base_net.6/base_net.6.0/Conv                             | Conv             | 2607us   |
I (3129) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3139) dl::Model: | /regression_headers.0/regression_headers.0.0/Conv         | Conv             | 2583us   |
I (3149) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3159) dl::Model: | /base_net.7/base_net.7.0/Conv                             | Conv             | 2579us   |
I (3169) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3179) dl::Model: | /base_net.10/base_net.10.2/Conv                           | Conv             | 2563us   |
I (3189) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3199) dl::Model: | /classification_headers.0/classification_headers.0.0/Conv | Conv             | 2542us   |
I (3209) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3219) dl::Model: | /base_net.9/base_net.9.2/Conv                             | Conv             | 2525us   |
I (3229) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3239) dl::Model: | /base_net.4/base_net.4.2/Conv                             | Conv             | 2245us   |
I (3249) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3259) dl::Model: | /base_net.8/base_net.8.2/Conv                             | Conv             | 1574us   |
I (3269) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3269) dl::Model: | /base_net.11/base_net.11.2/Conv                           | Conv             | 1554us   |
I (3279) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3289) dl::Model: | /base_net.8/base_net.8.0/Conv                             | Conv             | 1496us   |
I (3299) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3309) dl::Model: | /regression_headers.1/regression_headers.1.2/Conv         | Conv             | 1481us   |
I (3319) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3329) dl::Model: | /regression_headers.1/regression_headers.1.0/Conv         | Conv             | 1377us   |
I (3339) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3349) dl::Model: | /classification_headers.1/classification_headers.1.0/Conv | Conv             | 1171us   |
I (3359) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3369) dl::Model: | /base_net.9/base_net.9.0/Conv                             | Conv             | 1149us   |
I (3379) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3389) dl::Model: | /base_net.10/base_net.10.0/Conv                           | Conv             | 1147us   |
I (3399) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3409) dl::Model: | /regression_headers.3/Conv                                | Conv             | 1117us   |
I (3419) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3429) dl::Model: | /classification_headers.1/classification_headers.1.2/Conv | Conv             | 1009us   |
I (3439) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3449) dl::Model: | /extras.0/extras.0.0/Conv                                 | Conv             | 808us    |
I (3459) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3469) dl::Model: | /classification_headers.2/classification_headers.2.0/Conv | Conv             | 611us    |
I (3479) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3489) dl::Model: | /base_net.11/base_net.11.0/Conv                           | Conv             | 604us    |
I (3499) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3509) dl::Model: | /regression_headers.2/regression_headers.2.2/Conv         | Conv             | 595us    |
I (3519) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3529) dl::Model: | /classification_headers.3/Conv                            | Conv             | 593us    |
I (3539) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3549) dl::Model: | /base_net.12/base_net.12.0/Conv                           | Conv             | 522us    |
I (3559) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3569) dl::Model: | /classification_headers.2/classification_headers.2.2/Conv | Conv             | 482us    |
I (3579) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3589) dl::Model: | /Concat_1                                                 | Concat           | 449us    |
I (3599) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3609) dl::Model: | /regression_headers.2/regression_headers.2.0/Conv         | Conv             | 447us    |
I (3619) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3629) dl::Model: | /extras.0/extras.0.2/extras.0.2.2/Conv                    | Conv             | 444us    |
I (3639) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3649) dl::Model: | /Concat                                                   | Concat           | 156us    |
I (3659) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3669) dl::Model: | /extras.0/extras.0.2/extras.0.2.0/Conv                    | Conv             | 62us     |
I (3679) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3689) dl::Model: | PPQ_Operation_0                                           | RequantizeLinear | 49us     |
I (3699) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3709) dl::Model: | PPQ_Operation_1                                           | RequantizeLinear | 20us     |
I (3719) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3729) dl::Model: | PPQ_Operation_2                                           | RequantizeLinear | 15us     |
I (3739) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3749) dl::Model: | /Reshape                                                  | Reshape          | 9us      |
I (3759) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3769) dl::Model: | /Reshape_2                                                | Reshape          | 6us      |
I (3779) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3779) dl::Model: | /Reshape_1                                                | Reshape          | 5us      |
I (3789) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3799) dl::Model: | /Reshape_5                                                | Reshape          | 4us      |
I (3809) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3819) dl::Model: | /Reshape_4                                                | Reshape          | 4us      |
I (3829) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3839) dl::Model: | /Reshape_7                                                | Reshape          | 3us      |
I (3849) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3859) dl::Model: | /Reshape_3                                                | Reshape          | 3us      |
I (3869) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (3879) dl::Model: | /Reshape_6                                                | Reshape          | 2us      |
I (3889) dl::Model: +-----------------------------------------------------------+------------------+----------+

I (3899) DL_TEST: Starting inference...
I (4069) DL_TEST: Inference done in 38536412 cycles (approx 160.57 ms)
I (4069) DL_TEST: First 5 raw_conf: 34, -34, 35, -35, 35
I (4069) main_task: Returned from app_main()
``` 

### 160x120（第二版）
```js

I (1050) dl::Model: model:main_graph, version:0
I (1050) dl::Model: MODEL LOCATION IN FLASH PARTITION
I (1060) dl::Model: +----------------+--------------+-----------+--------------+
I (1060) dl::Model: |                      memory summary                      |
I (1070) dl::Model: +----------------+--------------+-----------+--------------+
I (1080) dl::Model: |                | internal RAM | PSRAM     | FLASH        |
I (1080) dl::Model: +----------------+--------------+-----------+--------------+
I (1090) dl::Model: | fbs_model      |              |           | 420.16KB     |
I (1100) dl::Model: |  -- parameter  |              |           |  -- 262.62KB |
I (1100) dl::Model: +----------------+--------------+-----------+--------------+
I (1110) dl::Model: | parameter_copy |              | 262.62KB  |              |
I (1120) dl::Model: +----------------+--------------+-----------+--------------+
I (1130) dl::Model: | variable       |              | 1275.00KB |              |
I (1130) dl::Model: +----------------+--------------+-----------+--------------+
I (1140) dl::Model: | others         | 31.30KB      | 11.27KB   |              |
I (1150) dl::Model: +----------------+--------------+-----------+--------------+
I (1150) dl::Model: | total          | 31.30KB      | 1548.90KB | 420.16KB     |
I (1160) dl::Model: +----------------+--------------+-----------+--------------+

I (1340) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1340) dl::Model: |                                     module summary                                      |
I (1350) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1360) dl::Model: | name                                                      | type             | latency  |
I (1370) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1380) dl::Model: | total                                                     |                  | 161944us |
I (1390) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1400) dl::Model: | /base_net.0/base_net.0.0/Conv                             | Conv             | 28860us  |
I (1410) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1420) dl::Model: | /base_net.1/base_net.1.2/Conv                             | Conv             | 20512us  |
I (1430) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1440) dl::Model: | /base_net.1/base_net.1.0/Conv                             | Conv             | 12473us  |
I (1450) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1460) dl::Model: | /base_net.2/base_net.2.0/Conv                             | Conv             | 11268us  |
I (1470) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1480) dl::Model: | /base_net.12/base_net.12.2/Conv                           | Conv             | 10251us  |
I (1490) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1500) dl::Model: | /base_net.3/base_net.3.0/Conv                             | Conv             | 5990us   |
I (1510) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1520) dl::Model: | /base_net.2/base_net.2.2/Conv                             | Conv             | 5583us   |
I (1520) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1530) dl::Model: | /base_net.3/base_net.3.2/Conv                             | Conv             | 5574us   |
I (1540) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1550) dl::Model: | /regression_headers.0/regression_headers.0.2/Conv         | Conv             | 5184us   |
I (1560) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1570) dl::Model: | /base_net.7/base_net.7.2/Conv                             | Conv             | 3316us   |
I (1580) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1590) dl::Model: | /base_net.5/base_net.5.2/Conv                             | Conv             | 3286us   |
I (1600) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1610) dl::Model: | /base_net.6/base_net.6.2/Conv                             | Conv             | 3285us   |
I (1620) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1630) dl::Model: | /classification_headers.0/classification_headers.0.2/Conv | Conv             | 3034us   |
I (1640) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1650) dl::Model: | /base_net.4/base_net.4.0/Conv                             | Conv             | 2935us   |
I (1660) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1670) dl::Model: | /base_net.5/base_net.5.0/Conv                             | Conv             | 2703us   |
I (1680) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1690) dl::Model: | /base_net.6/base_net.6.0/Conv                             | Conv             | 2635us   |
I (1700) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1710) dl::Model: | /regression_headers.0/regression_headers.0.0/Conv         | Conv             | 2619us   |
I (1720) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1730) dl::Model: | /base_net.7/base_net.7.0/Conv                             | Conv             | 2611us   |
I (1740) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1750) dl::Model: | /base_net.10/base_net.10.2/Conv                           | Conv             | 2600us   |
I (1760) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1770) dl::Model: | /classification_headers.0/classification_headers.0.0/Conv | Conv             | 2569us   |
I (1780) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1790) dl::Model: | /base_net.9/base_net.9.2/Conv                             | Conv             | 2548us   |
I (1800) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1810) dl::Model: | /base_net.4/base_net.4.2/Conv                             | Conv             | 2277us   |
I (1820) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1830) dl::Model: | /base_net.11/base_net.11.2/Conv                           | Conv             | 1605us   |
I (1840) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1850) dl::Model: | /base_net.8/base_net.8.2/Conv                             | Conv             | 1603us   |
I (1860) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1870) dl::Model: | /base_net.8/base_net.8.0/Conv                             | Conv             | 1522us   |
I (1880) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1890) dl::Model: | /regression_headers.1/regression_headers.1.2/Conv         | Conv             | 1488us   |
I (1900) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1910) dl::Model: | /regression_headers.1/regression_headers.1.0/Conv         | Conv             | 1411us   |
I (1920) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1930) dl::Model: | /classification_headers.1/classification_headers.1.0/Conv | Conv             | 1222us   |
I (1940) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1950) dl::Model: | /base_net.9/base_net.9.0/Conv                             | Conv             | 1195us   |
I (1960) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1970) dl::Model: | /base_net.10/base_net.10.0/Conv                           | Conv             | 1184us   |
I (1980) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (1990) dl::Model: | /regression_headers.3/Conv                                | Conv             | 1139us   |
I (2000) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2010) dl::Model: | /classification_headers.1/classification_headers.1.2/Conv | Conv             | 1028us   |
I (2020) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2030) dl::Model: | /extras.0/extras.0.0/Conv                                 | Conv             | 864us    |
I (2030) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2040) dl::Model: | /base_net.11/base_net.11.0/Conv                           | Conv             | 667us    |
I (2050) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2060) dl::Model: | /classification_headers.2/classification_headers.2.0/Conv | Conv             | 661us    |
I (2070) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2080) dl::Model: | /regression_headers.2/regression_headers.2.2/Conv         | Conv             | 625us    |
I (2090) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2100) dl::Model: | /classification_headers.3/Conv                            | Conv             | 609us    |
I (2110) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2120) dl::Model: | /base_net.12/base_net.12.0/Conv                           | Conv             | 590us    |
I (2130) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2140) dl::Model: | /classification_headers.2/classification_headers.2.2/Conv | Conv             | 519us    |
I (2150) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2160) dl::Model: | /regression_headers.2/regression_headers.2.0/Conv         | Conv             | 508us    |
I (2170) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2180) dl::Model: | /extras.0/extras.0.2/extras.0.2.2/Conv                    | Conv             | 476us    |
I (2190) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2200) dl::Model: | /Concat_1                                                 | Concat           | 457us    |
I (2210) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2220) dl::Model: | /Concat                                                   | Concat           | 165us    |
I (2230) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2240) dl::Model: | /extras.0/extras.0.2/extras.0.2.0/Conv                    | Conv             | 87us     |
I (2250) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2260) dl::Model: | PPQ_Operation_0                                           | RequantizeLinear | 49us     |
I (2270) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2280) dl::Model: | PPQ_Operation_3                                           | RequantizeLinear | 45us     |
I (2290) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2300) dl::Model: | PPQ_Operation_4                                           | RequantizeLinear | 24us     |
I (2310) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2320) dl::Model: | PPQ_Operation_2                                           | RequantizeLinear | 20us     |
I (2330) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2340) dl::Model: | PPQ_Operation_1                                           | RequantizeLinear | 20us     |
I (2350) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2360) dl::Model: | PPQ_Operation_5                                           | RequantizeLinear | 15us     |
I (2370) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2380) dl::Model: | /Reshape                                                  | Reshape          | 9us      |
I (2390) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2400) dl::Model: | /Reshape_2                                                | Reshape          | 6us      |
I (2410) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2420) dl::Model: | /Reshape_1                                                | Reshape          | 5us      |
I (2430) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2440) dl::Model: | /Reshape_4                                                | Reshape          | 4us      |
I (2450) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2460) dl::Model: | /Reshape_3                                                | Reshape          | 3us      |
I (2470) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2480) dl::Model: | /Reshape_7                                                | Reshape          | 2us      |
I (2490) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2500) dl::Model: | /Reshape_6                                                | Reshape          | 2us      |
I (2510) dl::Model: +-----------------------------------------------------------+------------------+----------+
I (2520) dl::Model: | /Reshape_5                                                | Reshape          | 2us      |
I (2530) dl::Model: +-----------------------------------------------------------+------------------+----------+
```