#pragma once

#include "app_types.h"

void fetchNordPoolPriceInfo(
    const char *apiBaseUrl,
    const char *area,
    const char *currency,
    uint16_t resolutionMinutes,
    PriceState &out);
void nordPoolPreupdateMovingAverageFromPriceInfo(PriceState &state);
