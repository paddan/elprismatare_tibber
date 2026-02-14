#include <Arduino.h>
#include <stdarg.h>

#include "logging_utils.h"

constexpr bool kDebugLogEnabled = true;

void logf(const char *fmt, ...) {
  if (!kDebugLogEnabled) return;
  char message[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  Serial.printf("[%10lu] %s\n", millis(), message);
}
