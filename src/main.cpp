// agri-canopy-poe — M5Stack PoECAM (ESP32-WROVER + W5500 + OV2640) node.
//
// - Solar irradiance from M5 ADC Unit V1.1 (ADS1110) + PVSS-03 on Grove
//   (SDA=G25, SCL=G33), published to MQTT (agriha 1値1トピック
//   InRadiation) and optionally UECS-CCM.
// - OV2640 camera capture on cadence, uploaded via HTTP PUT to a WebDAV
//   endpoint. On upload success the auth-free view URL is published to
//   <prefix>/sensor/CamShot as a retained {value:"<url>",unit:"url",ts}.
//
// LED status is not driven — GPIO 27 is camera XCLK on this board, and
// core's agri::Led is hard-coded to WS2812@G27. Status is logged over
// serial instead.

#include <Arduino.h>
#include <Wire.h>
#include <AgriNode.h>

#include "config.h"
#include "sensors.h"
#include "cam.h"
#include "webdav_up.h"
#include "mqtt_pub.h"
#include "ccm_pub.h"

const char *FW_NAME     = "agri-canopy-poe";
const char *FW_VERSION  = "0.2.0";
const char *FW_REPO     = "yasunorioi/agri-canopy-poe";
const char *FW_BIN_NAME = "agri-canopy-poe.bin";

AppConfig g_cfg;

bool  g_ads_ok    = false;
bool  g_cam_ok    = false;
float g_voltage   = 0.0f;
float g_solar_wm2 = NAN;

// Last successful upload — kept so the dashboard can show it and MQTT can
// re-publish the retained value after reconnect.
String   g_last_cam_url;
uint32_t g_last_cam_ms   = 0;
uint32_t g_last_cam_size = 0;

// ---------------------------------------------------------------- Web UI ----
static String renderDashboardSensors() {
  String s; s.reserve(512);
  char buf[16];

  s = F("<h3>Solar radiation</h3><table>");
  if (g_ads_ok) {
    dtostrf(isnan(g_solar_wm2) ? 0.0f : g_solar_wm2, 1, 1, buf);
    s += "<tr><th>Irradiance</th><td>"; s += buf; s += " W/m² → InRadiation</td></tr>";
    dtostrf(g_voltage, 1, 4, buf);
    s += "<tr><th>ADC voltage</th><td>"; s += buf; s += " V</td></tr>";
    s += "<tr><th>Calibration</th><td>"; s += g_cfg.wm2_per_volt; s += " W/m² per V</td></tr>";
  } else {
    s += "<tr><th>ADS1110</th><td>NOT detected</td></tr>";
  }
  s += F("</table>");

  s += F("<h3>Camera</h3><table>");
  s += "<tr><th>Sensor</th><td>"; s += (g_cam_ok ? "OV2640 OK" : "NOT initialised"); s += "</td></tr>";
  s += "<tr><th>Enabled</th><td>"; s += (g_cfg.cam_en ? "yes" : "no"); s += "</td></tr>";
  s += "<tr><th>Interval</th><td>"; s += g_cfg.cam_interval_s; s += " s</td></tr>";
  if (g_last_cam_url.length()) {
    s += "<tr><th>Last upload</th><td>";
    s += (millis() - g_last_cam_ms) / 1000; s += " s ago, ";
    s += g_last_cam_size; s += " bytes<br>";
    s += "<a href='"; s += g_last_cam_url; s += "' target=_blank>";
    s += g_last_cam_url; s += "</a></td></tr>";
  } else {
    s += "<tr><th>Last upload</th><td>(never)</td></tr>";
  }
  s += F("</table>");
  return s;
}

static String renderConfigSensorRows() {
  String s;
  auto row = [&](const char *label, const String &input) {
    s += "<tr><th>"; s += label; s += "</th><td>"; s += input; s += "</td></tr>";
  };
  // Solar calibration + CCM order
  row("W/m² per V (PVSS-03=1000)",
      "<input type=number name=wm2v value='" + String(g_cfg.wm2_per_volt) + "'>");
  row("CCM Order (InRadiation)",
      "<input type=number name=ccm_ord value='" + String(g_cfg.ccm_order) + "'>");

  // Camera
  row("Camera enabled",
      String("<input type=checkbox name=cam_en") + (g_cfg.cam_en ? " checked" : "") + ">");
  row("Camera interval (s)",
      "<input type=number name=cam_int_s value='" + String(g_cfg.cam_interval_s) + "'>");
  String resSel = "<select name=cam_res>";
  auto opt = [&](uint8_t v, const char *lbl){
    resSel += "<option value='"; resSel += v; resSel += "'";
    if (g_cfg.cam_res == v) resSel += " selected";
    resSel += ">"; resSel += lbl; resSel += "</option>";
  };
  opt(CAM_RES_VGA,  "VGA 640x480");
  opt(CAM_RES_SVGA, "SVGA 800x600");
  opt(CAM_RES_XGA,  "XGA 1024x768");
  opt(CAM_RES_HD,   "HD 1280x720");
  opt(CAM_RES_SXGA, "SXGA 1280x1024");
  opt(CAM_RES_UXGA, "UXGA 1600x1200");
  resSel += "</select>";
  row("Camera resolution", resSel);
  row("JPEG quality (10=best, 30=small)",
      "<input type=number name=cam_jq value='" + String(g_cfg.cam_jpeg_q) + "'>");
  row("Daylight-only capture",
      String("<input type=checkbox name=cam_dl_only") + (g_cfg.cam_daylight_only ? " checked" : "") + ">");
  row("Daylight threshold (W/m²)",
      "<input type=number name=cam_dl_wm2 value='" + String(g_cfg.cam_daylight_wm2) + "'>");

  // WebDAV
  row("WebDAV upload URL",
      "<input type=text size=48 name=wd_url value='" + String(g_cfg.wd_url) + "'>");
  row("WebDAV user",
      "<input type=text name=wd_user value='" + String(g_cfg.wd_user) + "'>");
  row("WebDAV password",
      "<input type=password name=wd_pass value='" + String(g_cfg.wd_pass) + "'>");
  return s;
}

static void applyConfigSensorForm(const String &body) {
  g_cfg.wm2_per_volt      = (uint16_t)agri::parseFormInt(body, "wm2v",       g_cfg.wm2_per_volt);
  g_cfg.ccm_order         = (int16_t) agri::parseFormInt(body, "ccm_ord",    g_cfg.ccm_order);
  g_cfg.cam_en            = agri::parseFormBool(body, "cam_en");
  g_cfg.cam_interval_s    = (uint32_t)agri::parseFormInt(body, "cam_int_s",  g_cfg.cam_interval_s);
  g_cfg.cam_res           = (uint8_t) agri::parseFormInt(body, "cam_res",    g_cfg.cam_res);
  g_cfg.cam_jpeg_q        = (uint8_t) agri::parseFormInt(body, "cam_jq",     g_cfg.cam_jpeg_q);
  g_cfg.cam_daylight_only = agri::parseFormBool(body, "cam_dl_only");
  g_cfg.cam_daylight_wm2  = (uint16_t)agri::parseFormInt(body, "cam_dl_wm2", g_cfg.cam_daylight_wm2);
  agri::parseFormStr(body, "wd_url",  g_cfg.wd_url,  sizeof(g_cfg.wd_url));
  agri::parseFormStr(body, "wd_user", g_cfg.wd_user, sizeof(g_cfg.wd_user));
  agri::parseFormStr(body, "wd_pass", g_cfg.wd_pass, sizeof(g_cfg.wd_pass));
  camReapplySettings();
}

static void addStatusFields(JsonObject doc) {
  doc["sensor_ok"] = g_ads_ok;
  doc["cam_ok"]    = g_cam_ok;
  if (g_ads_ok) {
    doc["voltage_v"] = g_voltage;
    doc["solar_wm2"] = isnan(g_solar_wm2) ? 0.0f : g_solar_wm2;
  }
  if (g_last_cam_url.length()) {
    doc["last_cam_url"]  = g_last_cam_url;
    doc["last_cam_ms"]   = g_last_cam_ms;
    doc["last_cam_size"] = g_last_cam_size;
  }
}

// ---------------------------------------------------------------- Camera ----
// Return true if the daylight gate lets us capture now.
static bool daylightOK() {
  if (!g_cfg.cam_daylight_only) return true;
  if (!g_ads_ok || isnan(g_solar_wm2)) return true;   // no reading = allow
  return g_solar_wm2 >= (float)g_cfg.cam_daylight_wm2;
}

// Grab a frame and PUT it. Called on cam_interval_s cadence when enabled.
static void captureAndUpload() {
  if (!g_cam_ok) return;
  if (!g_cfg.wd_url[0]) { Serial.println("[CAM] skip — wd_url empty"); return; }
  if (!daylightOK())    { Serial.println("[CAM] skip — below daylight"); return; }

  camera_fb_t *fb = camCapture();
  if (!fb) { Serial.println("[CAM] fb_get failed"); return; }

  String url = webdavUpload(fb->buf, fb->len);
  uint32_t size = fb->len;
  esp_camera_fb_return(fb);

  if (url.length()) {
    g_last_cam_url  = url;
    g_last_cam_ms   = millis();
    g_last_cam_size = size;
    mqttPublishCamShot(url);
  }
}

// -------------------------------------------------------------------- ----
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  loadConfig();
  Serial.printf("[CFG] node=%s host=%s mqtt=%s wd=%s cam=%s int=%us\n",
                g_cfg.common.node_id,
                g_cfg.common.hostname,
                g_cfg.common.mqtt_host[0] ? g_cfg.common.mqtt_host : "(unset)",
                g_cfg.wd_url[0] ? g_cfg.wd_url : "(unset)",
                g_cfg.cam_en ? "on" : "off",
                (unsigned)g_cfg.cam_interval_s);

  sensorsBegin();

  agri::W5500Pins pins;
  pins.sck = 23; pins.miso = 38; pins.mosi = 13; pins.cs = 4;
  agri::Network::begin(g_cfg.common.hostname, pins);
  agri::Network::waitForLease();

  // JST filename convention for the WebDAV archive (Japan farm context).
  configTime(9 * 3600, 0, "pool.ntp.org", "ntp.nict.jp");

  camBegin();

  agri::ccmBegin();
  agri::MQTT::begin();

  agri::WebHooks hooks;
  hooks.nodeTitle              = [](){ return FW_NAME; };
  hooks.renderDashboardSensors = renderDashboardSensors;
  hooks.renderConfigSensorRows = renderConfigSensorRows;
  hooks.applyConfigSensorForm  = applyConfigSensorForm;
  hooks.addStatusFields        = addStatusFields;
  hooks.saveConfig             = [](){ saveConfig(); };
  agri::WebUI::begin(g_cfg.common, hooks, FW_NAME, FW_VERSION);

  agri::mdnsBegin(g_cfg.common.hostname);
  agri::otaBegin(g_cfg.common.hostname);

  agri::OTA::begin(FW_REPO, FW_BIN_NAME, FW_VERSION);
  agri::OTA::checkLatest();

  Serial.println("[BOOT] ready");
}

void loop() {
  agri::otaHandle();
  agri::OTA::poll();
  agri::WebUI::handle(agri::Network::link_up, agri::Network::have_lease);

  uint32_t now = millis();

  static uint32_t lastSensorPoll = 0;
  if (now - lastSensorPoll >= 1000) {
    lastSensorPoll = now;
    sensorsPoll();
  }

  if (agri::networkUp() && agri::MQTT::hasHost(g_cfg.common)) {
    if (!agri::MQTT::connected()) {
      static uint32_t lastTry = 0;
      if (now - lastTry > 5000) { lastTry = now; agri::MQTT::reconnect(g_cfg.common); }
    } else {
      agri::MQTT::loop();
      static uint32_t lastPub = 0;
      uint32_t interval = (uint32_t)g_cfg.common.mqtt_interval_s * 1000UL;
      if (now - lastPub >= interval) {
        lastPub = now;
        mqttPublishSolar();
      }
    }
  }

  if (agri::networkUp() && g_cfg.common.ccm_enabled) {
    static uint32_t lastCcm = 0;
    uint32_t interval = (uint32_t)g_cfg.common.ccm_interval_s * 1000UL;
    if (now - lastCcm >= interval) {
      lastCcm = now;
      ccmPublish();
    }
  }

  // Camera cadence — first shot 30 s after boot (SNTP ideally has synced),
  // then every cam_interval_s.
  static uint32_t lastCam = 0;
  static bool     firstShot = false;
  if (g_cfg.cam_en && agri::networkUp()) {
    uint32_t due = firstShot ? (uint32_t)g_cfg.cam_interval_s * 1000UL : 30UL * 1000UL;
    if (now - lastCam >= due) {
      lastCam = now;
      firstShot = true;
      captureAndUpload();
    }
  }

  static uint32_t lastStatus = 0;
  if (now - lastStatus >= 30000) {
    lastStatus = now;
    Serial.printf("[STATUS] link=%d lease=%d mqtt=%d ads=%d cam=%d V=%.4f wm2=%.1f up=%lus\n",
                  agri::Network::link_up, agri::Network::have_lease,
                  agri::MQTT::connected(), g_ads_ok, g_cam_ok, g_voltage,
                  isnan(g_solar_wm2) ? 0.0f : g_solar_wm2,
                  (unsigned long)(now / 1000));
  }

  delay(20);
}
