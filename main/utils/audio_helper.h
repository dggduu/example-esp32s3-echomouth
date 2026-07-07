#ifndef __AUDIO_HELPER_H__
#define __AUDIO_HELPER_H__

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_helper_acquire(void);
void audio_helper_release(void);
bool audio_helper_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
