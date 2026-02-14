#include <WiFi.h>

#include "logging_utils.h"
#include "wifi_utils.h"

bool wifiConnect(const char *ssid, const char *password, uint32_t timeoutMs) {
  logf("WiFi connect start: SSID='%s'", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    logf("WiFi connected: ip=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }

  logf("WiFi connect timeout: status=%d", WiFi.status());
  return false;
}
