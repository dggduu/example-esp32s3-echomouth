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
