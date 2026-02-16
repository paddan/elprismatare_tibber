#pragma once

#include <Arduino.h>

constexpr size_t kMaxPoints = 240;

struct PricePoint {
  String startsAt;
  String level;
  float price = 0.0f;
};

struct PriceState {
  bool ok = false;
  String error;
  String source = "UNKNOWN";
  bool hasRunningAverage = false;
  float runningAverage = 0.0f;
  String currency = "SEK";
  uint16_t resolutionMinutes = 60;
  String currentStartsAt;
  String currentLevel = "UNKNOWN";
  float currentPrice = 0.0f;
  int currentIndex = -1;
  size_t count = 0;
  PricePoint points[kMaxPoints];
};
