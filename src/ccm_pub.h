// ccm_pub.h — solar (InRadiation) UECS-CCM publisher.
// Camera doesn't ride CCM — it goes over MQTT only.

#pragma once

#include <Arduino.h>
#include <AgriNode.h>
#include "config.h"
#include "sensors.h"

inline bool ccmPublish() {
  if (!g_cfg.common.ccm_enabled || !g_ads_ok || isnan(g_solar_wm2)) return false;

  char buf[12];
  dtostrf(g_solar_wm2, 1, 1, buf);

  String xml = agri::ccmEnvelopeOpen();
  xml += agri::ccmDatum("InRadiation",
                        g_cfg.common.ccm_room, g_cfg.common.ccm_region,
                        g_cfg.ccm_order, g_cfg.common.ccm_priority, buf);
  xml += agri::ccmEnvelopeClose();
  return agri::ccmSend(xml);
}
