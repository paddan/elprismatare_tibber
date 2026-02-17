#include "price_state_utils.h"

#include <math.h>

namespace {
bool isSamePoint(const PricePoint &lhs, const PricePoint &rhs) {
  return lhs.startsAt == rhs.startsAt && lhs.level == rhs.level && fabsf(lhs.price - rhs.price) < 0.0005f;
}

size_t dayCount(const PriceState &state) {
  if (!state.ok || state.count == 0) return 0;

  size_t uniqueDays = 0;
  String lastDay = "";
  for (size_t i = 0; i < state.count; ++i) {
    if (state.points[i].startsAt.length() < 10) continue;
    const String day = state.points[i].startsAt.substring(0, 10);
    if (day != lastDay) {
      lastDay = day;
      ++uniqueDays;
    }
  }
  return uniqueDays;
}
}  // namespace

bool hasNewPriceInfo(const PriceState &fetched, const PriceState &current) {
  if (!fetched.ok || fetched.count == 0) return false;
  if (!current.ok || current.count == 0) return true;
  if (fetched.count != current.count) return true;

  for (size_t i = 0; i < fetched.count; ++i) {
    if (!isSamePoint(fetched.points[i], current.points[i])) return true;
  }
  return false;
}

bool wouldReduceCoverage(const PriceState &fetched, const PriceState &current) {
  if (!fetched.ok || !current.ok || current.count == 0) return false;
  if (fetched.count < current.count) return true;
  return dayCount(fetched) < dayCount(current);
}

