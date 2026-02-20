#pragma once

#include "app_types.h"

void fetchNordPoolPriceInfo(
    const char *apiBaseUrl,
    const char *area,
    const char *currency,
    uint16_t resolutionMinutes,
    float vatPercent,
    float fixedCostPerKwh,
    PriceState &out);
void nordPoolPreupdateMovingAverageFromPriceInfo(PriceState &state, float vatPercent, float fixedCostPerKwh);
bool nordPoolRecalculatePricesFromRaw(PriceState &state, float vatPercent, float fixedCostPerKwh);
