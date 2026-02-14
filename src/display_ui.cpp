#include <Arduino.h>
#include <TFT_eSPI.h>

#include "display_ui.h"

namespace {
TFT_eSPI tft;

String formatPrice(float value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f kr", value);
  return String(buf);
}

uint16_t levelColor(const String &level) {
  if (level == "VERY_CHEAP") return TFT_GREENYELLOW;
  if (level == "CHEAP") return TFT_GREEN;
  if (level == "NORMAL") return TFT_YELLOW;
  if (level == "EXPENSIVE") return TFT_ORANGE;
  if (level == "VERY_EXPENSIVE") return TFT_RED;
  return TFT_WHITE;
}

void hardResetController() {
#ifdef TFT_RST
#if (TFT_RST >= 0)
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(5);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(150);
#endif
#endif
}
}  // namespace

void displayInit() {
  hardResetController();
  tft.init();
  tft.writecommand(0x11);  // SLPOUT
  delay(120);
  tft.writecommand(0x29);  // DISPON
  delay(20);
  tft.setRotation(1);
}

void displayDrawStaticUi() {
  tft.fillScreen(TFT_BLACK);
}

void displayDrawPrices(const PriceState &state) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(false);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);

  if (!state.ok) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("Fetch failed", 160, 70);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString(state.error, 160, 96);
    return;
  }

  const String priceText = formatPrice(state.currentPrice);
  const uint16_t priceColor = levelColor(state.currentLevel);

  tft.setTextColor(priceColor, TFT_BLACK);
  tft.setTextFont(4);
  tft.setTextSize(3);
  tft.drawString(priceText, 160, 58);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  if (state.count == 0) return;

  const int chartX = 10;
  const int chartY = 145;
  const int chartW = 300;
  const int chartH = 78;
  tft.drawRect(chartX - 1, chartY - 1, chartW + 2, chartH + 2, TFT_DARKGREY);
  const int xAxisY = chartY + chartH - 1;
  tft.drawFastHLine(chartX, xAxisY, chartW, TFT_DARKGREY);

  float minPrice = state.points[0].price;
  float maxPrice = state.points[0].price;
  for (size_t i = 0; i < state.count; ++i) {
    if (state.points[i].price < minPrice) minPrice = state.points[i].price;
    if (state.points[i].price > maxPrice) maxPrice = state.points[i].price;
  }
  const float range = max(0.001f, maxPrice - minPrice);

  const int barW = max(2, chartW / (int)state.count);
  const int usedW = barW * (int)state.count;
  const int startX = chartX + (chartW - usedW) / 2;

  String lastDay = "";
  for (size_t i = 0; i < state.count; ++i) {
    const PricePoint &p = state.points[i];
    const int x = startX + (int)i * barW;
    const int w = max(1, barW - 1);
    const float normalized = (p.price - minPrice) / range;
    const int h = (int)(normalized * (chartH - 4));
    const int y = xAxisY - h;
    const bool isCurrent = ((int)i == state.currentIndex);
    const uint16_t barColor = isCurrent ? TFT_WHITE : levelColor(p.level);

    if (h > 0) {
      tft.fillRect(x, y, w, h, barColor);
    }

    if (p.startsAt.length() >= 10) {
      const String day = p.startsAt.substring(0, 10);
      if (day != lastDay) {
        lastDay = day;
        tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        tft.setTextFont(1);
        tft.drawString(p.startsAt.substring(8, 10) + "/" + p.startsAt.substring(5, 7), x, chartY - 10);
      }
    }
  }
}
