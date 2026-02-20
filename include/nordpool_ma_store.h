#pragma once

#include <Arduino.h>
#include <stdint.h>

constexpr uint16_t kMovingAverageWindowHours = 72;
constexpr uint16_t kMaxMovingAverageWindowSamples = kMovingAverageWindowHours * 4;  // 15-minute resolution
constexpr uint32_t kMovingAverageStoreMagic = 0x4E504D41;  // "NPMA"
constexpr uint16_t kMovingAverageStoreVersion = 3;

struct MovingAverageStore {
  uint32_t magic = kMovingAverageStoreMagic;
  uint16_t version = kMovingAverageStoreVersion;
  uint16_t resolutionMinutes = 60;
  uint16_t windowSamples = kMovingAverageWindowHours;
  uint16_t count = 0;
  uint16_t head = 0;  // next write index
  char lastSlotKey[20] = {0};  // YYYY-MM-DDTHH or YYYY-MM-DDTHH:MM
  // Raw market prices in major currency units per kWh.
  float values[kMaxMovingAverageWindowSamples] = {0.0f};
};

void resetMovingAverageStore(MovingAverageStore &store);
bool loadMovingAverageStore(MovingAverageStore &store);
bool saveMovingAverageStore(const MovingAverageStore &store);
bool clearMovingAverageStore();
void addMovingAverageSample(MovingAverageStore &store, float value);
float movingAverageValue(const MovingAverageStore &store);
