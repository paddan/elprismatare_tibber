#pragma once

#include <stdint.h>
#include <WString.h>

struct AppSecrets {
  String nordpoolArea;
  String nordpoolCurrency;
  uint16_t nordpoolResolutionMinutes = 60;
  float vatPercent = 25.0f;
  // Stored as minor currency units per kWh.
  float fixedCostPerKwh = 0.0f;
};

void loadAppSecrets(AppSecrets &out);
bool wifiConnectWithConfigPortal(AppSecrets &secrets, uint16_t portalTimeoutSeconds);
bool wifiReconnect(uint32_t timeoutMs);
void wifiResetSettings();
