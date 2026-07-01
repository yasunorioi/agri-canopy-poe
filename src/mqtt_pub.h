// mqtt_pub.h — agriha 1値1トピック publisher (matches agri-env-poe).
//
// Publishes InRadiation (from ADS1110/PVSS-03) and CamShot (URL of the
// most recent camera upload) as <prefix>/sensor/<Type> retained JSONs of
// the form {"value":<num-or-string>,"unit":"<unit>","ts":<epoch>}.

#pragma once

#include <Arduino.h>
#include <time.h>
#include <AgriNode.h>
#include "config.h"
#include "sensors.h"

inline long mqttNow() {
  time_t t = time(nullptr);
  return (t > 1700000000) ? (long)t : 0;
}

// value is inserted verbatim — pass numeric literal for scalars or a
// quoted string like "\"http://...\"" for URLs.
inline bool mqttPublishOne(const char *type, const char *unit,
                           const char *valueLiteral) {
  char topic[96];
  snprintf(topic, sizeof(topic), "%s/sensor/%s",
           g_cfg.common.mqtt_topic_prefix, type);
  char payload[256];
  int n = snprintf(payload, sizeof(payload),
                   "{\"value\":%s,\"unit\":\"%s\",\"ts\":%ld}",
                   valueLiteral, unit, mqttNow());
  bool ok = agri::MQTT::mqtt.publish(topic, (const uint8_t*)payload,
                                     (unsigned)n, /*retain=*/true);
  Serial.printf("[MQTT] %s %s %s\n", topic, valueLiteral, ok ? "OK" : "FAIL");
  return ok;
}

inline bool mqttPublishSolar() {
  if (!agri::MQTT::hasHost(g_cfg.common) || !agri::MQTT::connected()) return false;
  if (!g_ads_ok || isnan(g_solar_wm2)) return false;
  char buf[16];
  dtostrf(g_solar_wm2, 1, 1, buf);
  return mqttPublishOne("InRadiation", "W/m^2", buf);
}

// Publish the URL of the last uploaded image. Retained so ArSprout /
// dashboards can pick it up on connect.
inline bool mqttPublishCamShot(const String &url) {
  if (url.isEmpty()) return false;
  if (!agri::MQTT::hasHost(g_cfg.common) || !agri::MQTT::connected()) return false;
  String quoted = "\"" + url + "\"";
  return mqttPublishOne("CamShot", "url", quoted.c_str());
}
