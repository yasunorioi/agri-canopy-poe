// cam.h — OV2640 camera init + capture, wrapping esp_camera driver.
//
// Pin numbers are verified against a working PoECAM (ESP32-WROVER + W5500)
// project (vothmarkus/PrusaConnectCam-M5Stack-PoECam, Camera_cfg.h) and
// confirmed by the bring-up sketch at ~/poecam-bringup.

#pragma once

#include <Arduino.h>
#include "esp_camera.h"
#include "config.h"

// PoECAM (ESP32-WROVER) OV2640 DVP pin config
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  15
#define CAM_PIN_XCLK   27
#define CAM_PIN_SIOD   14   // SCCB SDA
#define CAM_PIN_SIOC   12   // SCCB SCL
#define CAM_PIN_D7     19
#define CAM_PIN_D6     36
#define CAM_PIN_D5     18
#define CAM_PIN_D4     39
#define CAM_PIN_D3      5
#define CAM_PIN_D2     34
#define CAM_PIN_D1     35
#define CAM_PIN_D0     32
#define CAM_PIN_VSYNC  22
#define CAM_PIN_HREF   26
#define CAM_PIN_PCLK   21

extern bool g_cam_ok;

inline bool camBegin() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0       = CAM_PIN_D0;
  c.pin_d1       = CAM_PIN_D1;
  c.pin_d2       = CAM_PIN_D2;
  c.pin_d3       = CAM_PIN_D3;
  c.pin_d4       = CAM_PIN_D4;
  c.pin_d5       = CAM_PIN_D5;
  c.pin_d6       = CAM_PIN_D6;
  c.pin_d7       = CAM_PIN_D7;
  c.pin_xclk     = CAM_PIN_XCLK;
  c.pin_pclk     = CAM_PIN_PCLK;
  c.pin_vsync    = CAM_PIN_VSYNC;
  c.pin_href     = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn     = CAM_PIN_PWDN;
  c.pin_reset    = CAM_PIN_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = (framesize_t)g_cfg.cam_res;
  c.jpeg_quality = g_cfg.cam_jpeg_q;
  c.fb_count     = 1;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  g_cam_ok = (err == ESP_OK);
  Serial.printf("[CAM] init %s (0x%x) res=%u q=%u\n",
                g_cam_ok ? "OK" : "FAILED", err,
                g_cfg.cam_res, g_cfg.cam_jpeg_q);
  return g_cam_ok;
}

// Re-apply resolution / quality from live g_cfg without a reboot. Called
// from /config save hook so the user can iterate.
inline void camReapplySettings() {
  if (!g_cam_ok) return;
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  s->set_framesize(s, (framesize_t)g_cfg.cam_res);
  s->set_quality  (s, g_cfg.cam_jpeg_q);
}

// Capture and return a framebuffer. Caller MUST call esp_camera_fb_return().
//
// fb_count==1 freezes the single buffer between our 30-min captures: right
// after the previous fb_return the driver grabs one frame and then, with no
// free buffer, stops — so the next fb_get would hand back that ~30-min-old
// frame (CAMERA_GRAB_LATEST is a no-op with fb_count==1). Discard a couple of
// frames first to force a capture of the current scene. The OV2640 is clocked
// continuously (XCLK always on, PWDN=-1), so auto-exposure is already tracking
// the live scene and the flushed frame is correctly exposed.
inline camera_fb_t *camCapture() {
  if (!g_cam_ok) return nullptr;
  for (int i = 0; i < 2; i++) {
    camera_fb_t *stale = esp_camera_fb_get();
    if (!stale) return nullptr;
    esp_camera_fb_return(stale);
  }
  return esp_camera_fb_get();
}
