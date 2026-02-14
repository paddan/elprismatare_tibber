#pragma once

#include <stdint.h>

bool wifiConnect(const char *ssid, const char *password, uint32_t timeoutMs);
