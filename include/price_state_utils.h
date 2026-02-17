#pragma once

#include "app_types.h"

bool hasNewPriceInfo(const PriceState &fetched, const PriceState &current);
bool wouldReduceCoverage(const PriceState &fetched, const PriceState &current);

