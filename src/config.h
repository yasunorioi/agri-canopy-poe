// config.h — agri-canopy-poe NVS-backed config.
//
// AppConfig = CommonConfig + camera (interval / resolution / quality /
// daylight gate) + lat/lon + WebDAV (URL / user / password). All editable
// from /config; empty MQTT host or WebDAV URL disables that path.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <AgriCommonConfig.h>

// Camera resolution enum — small so the Web UI dropdown stays tidy.
// Values map 1:1 to framesize_t (esp_camera.h): VGA=6, SVGA=7, XGA=8,
// HD=9, SXGA=10, UXGA=11.
enum CamRes : uint8_t {
  CAM_RES_VGA  = 6,   //  640 x  480
  CAM_RES_SVGA = 7,   //  800 x  600
  CAM_RES_XGA  = 8,   // 1024 x  768
  CAM_RES_HD   = 9,   // 1280 x  720
  CAM_RES_SXGA = 10,  // 1280 x 1024  (default)
  CAM_RES_UXGA = 11,  // 1600 x 1200
};

struct AppConfig {
  agri::CommonConfig common;
  // Camera
  bool     cam_en;
  uint32_t cam_interval_s;   // capture cadence
  uint8_t  cam_res;          // CamRes
  uint8_t  cam_jpeg_q;       // 10 (best) — 30 (small)
  bool     cam_daylight_only;
  float    lat;              // degrees, +N
  float    lon;              // degrees, +E
  float    sun_elev_min_deg; // gate threshold (sun above horizon)
  // WebDAV target
  char wd_url[96];           // base, e.g. "http://yasu-hp.local:8080/upload"
  char wd_user[32];
  char wd_pass[32];
};

extern AppConfig g_cfg;

inline void setDefaults() {
  agri::commonDefaults(g_cfg.common,
                       "canopy_node_01", "agri-canopy-01",
                       /*mqtt prefix = house scope*/ "agriha/2",
                       /*default_ccm_region = 別棟 ArSprout region*/13);
  g_cfg.cam_en            = true;
  g_cfg.cam_interval_s    = 1800;     // 30 min
  g_cfg.cam_res           = CAM_RES_SXGA;
  g_cfg.cam_jpeg_q        = 12;
  g_cfg.cam_daylight_only = true;
  g_cfg.lat               = 35.0f;    // rough JP centre — override in /config
  g_cfg.lon               = 135.0f;
  g_cfg.sun_elev_min_deg  = 5.0f;
  g_cfg.wd_url[0]  = '\0';
  g_cfg.wd_user[0] = '\0';
  g_cfg.wd_pass[0] = '\0';
}

inline void loadConfig() {
  setDefaults();
  Preferences p;
  if (!p.begin("canopy-cfg", true)) return;
  agri::commonLoad(g_cfg.common, p);
  g_cfg.cam_en            = p.getBool  ("cam_en",     g_cfg.cam_en);
  g_cfg.cam_interval_s    = p.getUInt  ("cam_int_s",  g_cfg.cam_interval_s);
  g_cfg.cam_res           = p.getUChar ("cam_res",    g_cfg.cam_res);
  g_cfg.cam_jpeg_q        = p.getUChar ("cam_jq",     g_cfg.cam_jpeg_q);
  g_cfg.cam_daylight_only = p.getBool  ("cam_dl_only",g_cfg.cam_daylight_only);
  g_cfg.lat               = p.getFloat ("lat",        g_cfg.lat);
  g_cfg.lon               = p.getFloat ("lon",        g_cfg.lon);
  g_cfg.sun_elev_min_deg  = p.getFloat ("sun_elev",   g_cfg.sun_elev_min_deg);
  auto loadStr = [&](const char *k, char *dst, size_t n) {
    String v = p.getString(k, dst);
    strlcpy(dst, v.c_str(), n);
  };
  loadStr("wd_url",  g_cfg.wd_url,  sizeof(g_cfg.wd_url));
  loadStr("wd_user", g_cfg.wd_user, sizeof(g_cfg.wd_user));
  loadStr("wd_pass", g_cfg.wd_pass, sizeof(g_cfg.wd_pass));
  p.end();
}

inline bool saveConfig() {
  Preferences p;
  if (!p.begin("canopy-cfg", false)) return false;
  agri::commonSave(g_cfg.common, p);
  p.putBool  ("cam_en",     g_cfg.cam_en);
  p.putUInt  ("cam_int_s",  g_cfg.cam_interval_s);
  p.putUChar ("cam_res",    g_cfg.cam_res);
  p.putUChar ("cam_jq",     g_cfg.cam_jpeg_q);
  p.putBool  ("cam_dl_only",g_cfg.cam_daylight_only);
  p.putFloat ("lat",        g_cfg.lat);
  p.putFloat ("lon",        g_cfg.lon);
  p.putFloat ("sun_elev",   g_cfg.sun_elev_min_deg);
  p.putString("wd_url",     g_cfg.wd_url);
  p.putString("wd_user",    g_cfg.wd_user);
  p.putString("wd_pass",    g_cfg.wd_pass);
  p.end();
  return true;
}
