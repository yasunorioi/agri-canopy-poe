// webdav_up.h — HTTP PUT client for the canopy image endpoint.
//
// Sends a captured JPEG to <wd_url>/<hostname>/<YYYY>/<MMDDHHMMSS>.jpg
// with Basic auth. nginx-extras' ngx_http_dav_module handles PUT with
// create_full_put_path on, so intermediate directories are auto-created.
//
// Returns the full URL that was uploaded on success (empty on failure) —
// caller publishes it as the `CamShot` MQTT value.

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <time.h>
#include "config.h"

// Build "<YYYY>/<MMDDHHMMSS>.jpg". Uses SNTP time — returns empty if the
// clock hasn't synced yet (upload should be skipped in that case, otherwise
// we'd pollute the archive with 1970 paths).
inline String buildImagePath() {
  time_t t = time(nullptr);
  if (t < 1700000000) return String();
  struct tm tm; localtime_r(&t, &tm);
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d/%02d%02d%02d%02d%02d.jpg",
           1900 + tm.tm_year,
           1 + tm.tm_mon, tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec);
  return String(buf);
}

// nginx-extras serves PUTs under an auth'd /upload/... location and GETs
// from an open /... location (both alias /srv/cam). We publish the auth-
// free view URL so dashboards / ArSprout can img-src it directly, so this
// strips a trailing /upload from the base before building the CamShot URL.
inline String viewBaseFromUploadUrl(const char *upload_url) {
  String base(upload_url);
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (base.endsWith("/upload")) base = base.substring(0, base.length() - 7);
  return base;
}

// PUT jpeg data to the configured WebDAV endpoint. Returns the auth-free
// view URL on success (for MQTT publish), empty String on failure.
inline String webdavUpload(const uint8_t *data, size_t len) {
  if (!g_cfg.wd_url[0] || !g_cfg.wd_user[0]) return String();

  String path = buildImagePath();
  if (path.isEmpty()) {
    Serial.println("[WD] skip — no SNTP time");
    return String();
  }

  String tail = String(g_cfg.common.hostname) + "/" + path;

  String putUrl(g_cfg.wd_url);
  if (!putUrl.endsWith("/")) putUrl += "/";
  putUrl += tail;

  HTTPClient http;
  if (!http.begin(putUrl)) {
    Serial.printf("[WD] begin failed: %s\n", putUrl.c_str());
    return String();
  }
  http.setAuthorization(g_cfg.wd_user, g_cfg.wd_pass);
  http.addHeader("Content-Type", "image/jpeg");
  http.setTimeout(15000);

  int code = http.sendRequest("PUT", (uint8_t*)data, len);
  http.end();

  Serial.printf("[WD] PUT %s → %d (%u bytes)\n", putUrl.c_str(), code, (unsigned)len);
  if (code < 200 || code >= 300) return String();

  return viewBaseFromUploadUrl(g_cfg.wd_url) + "/" + tail;
}
