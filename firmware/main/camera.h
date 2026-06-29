#pragma once
#include "esp_err.h"
#include "esp_camera.h"
#include "ring_buffer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

esp_err_t wearcam_cam_init(void);
bool cam_ready(void);
esp_err_t cam_set_config(framesize_t fs, int fps, int quality);
void cam_get_settings(int *fps, int *quality, int *width, int *height);
void cam_start_capture(ring_buffer_t *rb);

// Latest published JPEG frame, malloc'd copy (caller frees). NULL if none yet.
uint8_t *framehub_get(size_t *out_len);
