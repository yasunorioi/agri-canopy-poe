// sun.h — solar elevation from lat/lon/UTC (NOAA solar position algorithm).
//
// Replaces the ADS1110/PVSS-03 daylight gate. Enough precision for a
// "sun above X°" camera gate; not for astronomy.

#pragma once

#include <Arduino.h>
#include <math.h>
#include <time.h>

// Return solar elevation in degrees (negative = below horizon).
// NaN if SNTP hasn't synced yet.
inline float sunElevationDeg(float lat_deg, float lon_deg) {
  time_t t = time(nullptr);
  if (t < 1700000000) return NAN;

  struct tm utc;
  gmtime_r(&t, &utc);

  int   doy       = utc.tm_yday + 1;                         // 1..366
  float hour_utc  = utc.tm_hour + utc.tm_min / 60.0f
                                + utc.tm_sec / 3600.0f;

  // NOAA fractional year (radians)
  float gamma = 2.0f * (float)M_PI / 365.0f
              * ((doy - 1) + (hour_utc - 12.0f) / 24.0f);

  // Equation of time (minutes)
  float eqtime = 229.18f * ( 0.000075f
               + 0.001868f * cosf(gamma)   - 0.032077f * sinf(gamma)
               - 0.014615f * cosf(2*gamma) - 0.040849f * sinf(2*gamma));

  // Solar declination (radians)
  float decl = 0.006918f
             - 0.399912f * cosf(gamma)   + 0.070257f * sinf(gamma)
             - 0.006758f * cosf(2*gamma) + 0.000907f * sinf(2*gamma)
             - 0.002697f * cosf(3*gamma) + 0.00148f  * sinf(3*gamma);

  // True solar time (minutes) — lon east is positive
  float tst = hour_utc * 60.0f + eqtime + 4.0f * lon_deg;

  // Hour angle (radians)
  float ha = (tst / 4.0f - 180.0f) * (float)M_PI / 180.0f;

  float lat = lat_deg * (float)M_PI / 180.0f;
  float cos_zen = sinf(lat) * sinf(decl)
                + cosf(lat) * cosf(decl) * cosf(ha);
  if (cos_zen >  1.0f) cos_zen =  1.0f;
  if (cos_zen < -1.0f) cos_zen = -1.0f;
  return 90.0f - acosf(cos_zen) * 180.0f / (float)M_PI;
}
