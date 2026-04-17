
#ifndef FACE_DETECTOR_HELPER_H
#define FACE_DETECTOR_HELPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool face_detector_helper_init(int fb_width, int fb_height);

void face_detector_helper_deinit(void);

bool face_detector_helper_trigger_detection(uint32_t timeout_ms);

bool face_detector_helper_is_busy(void);

void face_detector_helper_update_timestamp(void);

bool face_detector_helper_has_recent_face(int max_age_ms);

#ifdef __cplusplus
}
#endif

#endif