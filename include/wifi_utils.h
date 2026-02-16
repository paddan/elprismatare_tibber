#pragma once

#include <stdint.h>
#include <WString.h>

struct AppSecrets {
  String nordpoolArea;
  String nordpoolCurrency;
  uint16_t nordpoolResolutionMinutes = 60;
};

void loadAppSecrets(AppSecrets &out);
bool wifiConnectWithConfigPortal(AppSecrets &secrets, uint16_t portalTimeoutSeconds);
bool wifiReconnect(uint32_t timeoutMs);
void wifiResetSettings();
