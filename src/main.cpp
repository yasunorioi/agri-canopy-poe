// agri-canopy-poe — M5Stack PoECAM (ESP32-WROVER + W5500 + OV2640) node.
//
// - Camera-only node. Daylight gate is derived from the NOAA solar
//   position algorithm using lat/lon and NTP-synced UTC (no ADC — the
//   ADS1110/PVSS-03 path was removed at v0.3.0 after site noise made
//   the analog reading unusable).
// - OV2640 capture on cadence, uploaded via HTTP PUT to a WebDAV
//   endpoint. On upload success the auth-free view URL is published to
//   <prefix>/sensor/CamShot as a retained {value:"<url>",unit:"url",ts}.
//
// LED status is not driven — GPIO 27 is camera XCLK on this board, and
// core's agri::Led is hard-coded to WS2812@G27. Status is logged over
// serial instead.

#include <Arduino.h>
#include <AgriNode.h>

#include "config.h"
#include "sun.h"
#include "cam.h"
#include "webdav_up.h"
#include "mqtt_pub.h"

const char *FW_NAME     = "agri-canopy-poe";
const char *FW_VERSION  = "0.3.0";
const char *FW_REPO     = "yasunorioi/agri-canopy-poe";
const char *FW_BIN_NAME = "agri-canopy-poe.bin";

AppConfig g_cfg;

bool  g_cam_ok       = false;
float g_sun_elev_deg = NAN;

// Last successful upload — kept so the dashboard can show it and MQTT can
// re-publish the retained value after reconnect.
String   g_last_cam_url;
uint32_t g_last_cam_ms   = 0;
uint32_t g_last_cam_size = 0;

// ---------------------------------------------------------------- Web UI ----
static String renderDashboardSensors() {
  String s; s.reserve(512);
  char buf[16];

  s = F("<h3>Sun position</h3><table>");
  if (isnan(g_sun_elev_deg)) {
    s += "<tr><th>Elevation</th><td>(waiting for SNTP)</td></tr>";
  } else {
    dtostrf(g_sun_elev_deg, 1, 2, buf);
    s += "<tr><th>Elevation</th><td>"; s += buf; s += " °</td></tr>";
  }
  dtostrf(g_cfg.lat, 1, 4, buf);
  s += "<tr><th>Latitude</th><td>";  s += buf; s += " °N</td></tr>";
  dtostrf(g_cfg.lon, 1, 4, buf);
  s += "<tr><th>Longitude</th><td>"; s += buf; s += " °E</td></tr>";
  dtostrf(g_cfg.sun_elev_min_deg, 1, 1, buf);
  s += "<tr><th>Gate threshold</th><td>&gt; "; s += buf; s += " °</td></tr>";
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
  row("Latitude (°N)",
      "<input type=text name=lat value='" + String(g_cfg.lat, 4) + "'>");
  row("Longitude (°E)",
      "<input type=text name=lon value='" + String(g_cfg.lon, 4) + "'>");
  row("Sun elevation gate (°)",
      "<input type=text name=sun_elev value='" + String(g_cfg.sun_elev_min_deg, 1) + "'>");

  // WebDAV
  row("WebDAV upload URL",
      "<input type=text size=48 name=wd_url value='" + String(g_cfg.wd_url) + "'>");
  row("WebDAV user",
      "<input type=text name=wd_user value='" + String(g_cfg.wd_user) + "'>");
  row("WebDAV password",
      "<input type=password name=wd_pass value='" + String(g_cfg.wd_pass) + "'>");
  return s;
}

// atof() via a scratch buffer — core has parseFormInt/Bool/Str but no float.
static float parseFormFloat(const String &body, const char *key, float defv) {
  char buf[24];
  buf[0] = '\0';
  agri::parseFormStr(body, key, buf, sizeof(buf));
  if (!buf[0]) return defv;
  return (float)atof(buf);
}

static void applyConfigSensorForm(const String &body) {
  g_cfg.cam_en            = agri::parseFormBool(body, "cam_en");
  g_cfg.cam_interval_s    = (uint32_t)agri::parseFormInt(body, "cam_int_s",  g_cfg.cam_interval_s);
  g_cfg.cam_res           = (uint8_t) agri::parseFormInt(body, "cam_res",    g_cfg.cam_res);
  g_cfg.cam_jpeg_q        = (uint8_t) agri::parseFormInt(body, "cam_jq",     g_cfg.cam_jpeg_q);
  g_cfg.cam_daylight_only = agri::parseFormBool(body, "cam_dl_only");
  g_cfg.lat               = parseFormFloat(body, "lat",      g_cfg.lat);
  g_cfg.lon               = parseFormFloat(body, "lon",      g_cfg.lon);
  g_cfg.sun_elev_min_deg  = parseFormFloat(body, "sun_elev", g_cfg.sun_elev_min_deg);
  agri::parseFormStr(body, "wd_url",  g_cfg.wd_url,  sizeof(g_cfg.wd_url));
  agri::parseFormStr(body, "wd_user", g_cfg.wd_user, sizeof(g_cfg.wd_user));
  agri::parseFormStr(body, "wd_pass", g_cfg.wd_pass, sizeof(g_cfg.wd_pass));
  camReapplySettings();
}

static void addStatusFields(JsonObject doc) {
  doc["cam_ok"] = g_cam_ok;
  if (!isnan(g_sun_elev_deg)) doc["sun_elev_deg"] = g_sun_elev_deg;
  if (g_last_cam_url.length()) {
    doc["last_cam_url"]  = g_last_cam_url;
    doc["last_cam_ms"]   = g_last_cam_ms;
    doc["last_cam_size"] = g_last_cam_size;
  }
}

// ---------------------------------------------------------------- Camera ----
// Return true if the daylight gate lets us capture now.
// If SNTP hasn't synced yet, allow (avoid dead-locking the camera on boot).
static bool daylightOK() {
  if (!g_cfg.cam_daylight_only) return true;
  if (isnan(g_sun_elev_deg))    return true;
  return g_sun_elev_deg >= g_cfg.sun_elev_min_deg;
}

// Grab a frame and PUT it. Called on cam_interval_s cadence when enabled.
static void captureAndUpload() {
  if (!g_cam_ok) return;
  if (!g_cfg.wd_url[0]) { Serial.println("[CAM] skip — wd_url empty"); return; }
  if (!daylightOK())    { Serial.println("[CAM] skip — sun below gate"); return; }

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
  Serial.printf("[CFG] node=%s host=%s mqtt=%s wd=%s cam=%s int=%us lat=%.4f lon=%.4f\n",
                g_cfg.common.node_id,
                g_cfg.common.hostname,
                g_cfg.common.mqtt_host[0] ? g_cfg.common.mqtt_host : "(unset)",
                g_cfg.wd_url[0] ? g_cfg.wd_url : "(unset)",
                g_cfg.cam_en ? "on" : "off",
                (unsigned)g_cfg.cam_interval_s,
                g_cfg.lat, g_cfg.lon);

  agri::W5500Pins pins;
  pins.sck = 23; pins.miso = 38; pins.mosi = 13; pins.cs = 4;
  agri::Network::begin(g_cfg.common.hostname, pins);
  agri::Network::waitForLease();

  // JST filename convention for the WebDAV archive (Japan farm context).
  configTime(9 * 3600, 0, "pool.ntp.org", "ntp.nict.jp");

  camBegin();

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

  // Refresh sun elevation once per minute — cheap and never changes faster.
  static uint32_t lastSun = 0;
  if (now - lastSun >= 60000UL || lastSun == 0) {
    lastSun = now ? now : 1;
    g_sun_elev_deg = sunElevationDeg(g_cfg.lat, g_cfg.lon);
  }

  if (agri::networkUp() && agri::MQTT::hasHost(g_cfg.common)) {
    if (!agri::MQTT::connected()) {
      static uint32_t lastTry = 0;
      if (now - lastTry > 5000) { lastTry = now; agri::MQTT::reconnect(g_cfg.common); }
    } else {
      agri::MQTT::loop();
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
    Serial.printf("[STATUS] link=%d lease=%d mqtt=%d cam=%d sun=%.1f up=%lus\n",
                  agri::Network::link_up, agri::Network::have_lease,
                  agri::MQTT::connected(), g_cam_ok,
                  isnan(g_sun_elev_deg) ? -99.0f : g_sun_elev_deg,
                  (unsigned long)(now / 1000));
  }

  delay(20);
}
