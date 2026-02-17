#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "display_ui.h"
#include "logging_utils.h"
#include "time_utils.h"
#include "wifi_utils.h"

namespace
{
  constexpr char kPrefsNamespace[] = "elcfg";
  constexpr char kAreaKey[] = "np_area";
  constexpr char kCurrencyKey[] = "np_curr";
  constexpr char kResolutionKey[] = "np_res";
  constexpr char kVatPercentKey[] = "np_vat";
  constexpr char kFixedCostPerKwhKey[] = "np_fixkwh";
  constexpr char kLegacyPriceMultiplierKey[] = "np_mult";
  constexpr char kLegacyFixedAddOreKey[] = "np_fixore";
  constexpr char kDefaultNordpoolArea[] = "SE3";
  constexpr char kDefaultNordpoolCurrency[] = "SEK";
  constexpr uint16_t kDefaultNordpoolResolutionMinutes = 60;
  constexpr float kDefaultVatPercent = 25.0f;
  constexpr float kDefaultFixedCostPerKwh = 0.0f;
  constexpr const char *kNordpoolAreas[] = {
      "SE1", "SE2", "SE3", "SE4", "NO1", "NO2", "NO3", "NO4", "NO5",
      "DK1", "DK2", "FI", "EE", "LV", "LT", "SYS"};
  constexpr const char *kNordpoolCurrencies[] = {"SEK", "EUR", "NOK", "DKK"};
  constexpr size_t kNordpoolAreaCount = sizeof(kNordpoolAreas) / sizeof(kNordpoolAreas[0]);
  constexpr size_t kNordpoolCurrencyCount = sizeof(kNordpoolCurrencies) / sizeof(kNordpoolCurrencies[0]);
  constexpr size_t kAreaMaxLen = 8;
  constexpr size_t kCurrencyMaxLen = 8;
  constexpr size_t kResolutionMaxLen = 4;
  constexpr size_t kVatPercentMaxLen = 16;
  constexpr size_t kFixedCostPerKwhMaxLen = 16;
  constexpr uint32_t kReconnectCooldownMs = 5000;

  bool gSaveConfigRequested = false;
  uint32_t gLastReconnectAttemptMs = 0;

  constexpr char kPortalCustomHead[] PROGMEM = R"HTML(
<script>
(function () {
  var areaOptions = ["SE1","SE2","SE3","SE4","NO1","NO2","NO3","NO4","NO5","DK1","DK2","FI","EE","LV","LT","SYS"];
  var currencyOptions = ["SEK","EUR","NOK","DKK"];
  var resolutionOptions = ["15","30","60"];

  function replaceInputWithSelect(inputId, options) {
    var input = document.getElementById(inputId);
    if (!input || input.tagName !== "INPUT") return;

    var selected = (input.value || "").toUpperCase();
    var select = document.createElement("select");
    select.id = input.id;
    select.name = input.name;
    select.style.width = "100%";

    var hasSelected = false;
    for (var i = 0; i < options.length; i++) {
      if (options[i] === selected) {
        hasSelected = true;
        break;
      }
    }
    if (!hasSelected && options.length > 0) {
      selected = options[0];
    }

    for (var j = 0; j < options.length; j++) {
      var option = document.createElement("option");
      option.value = options[j];
      option.text = options[j];
      select.appendChild(option);
    }

    select.value = selected;
    input.parentNode.replaceChild(select, input);
  }

  window.addEventListener("load", function () {
    replaceInputWithSelect("NordPoolArea", areaOptions);
    replaceInputWithSelect("NordPoolCurrency", currencyOptions);
    replaceInputWithSelect("NordPoolResolution", resolutionOptions);
  });
})();
</script>
)HTML";

  bool isAllowedToken(const String &value, const char *const *allowedValues, size_t allowedCount)
  {
    for (size_t i = 0; i < allowedCount; ++i)
    {
      if (value == allowedValues[i])
      {
        return true;
      }
    }
    return false;
  }

  String normalizeToken(
      String value,
      const char *fallback,
      size_t maxLen,
      const char *const *allowedValues,
      size_t allowedCount)
  {
    value.trim();
    value.toUpperCase();
    if (value.isEmpty())
    {
      value = fallback;
    }
    if (value.length() > maxLen)
    {
      value = value.substring(0, maxLen);
    }
    if (!isAllowedToken(value, allowedValues, allowedCount))
    {
      value = fallback;
    }
    return value;
  }

  uint16_t parseResolutionToken(const String &value)
  {
    String parsed = value;
    parsed.trim();
    return normalizeResolutionMinutes((uint16_t)parsed.toInt());
  }

  float parseFloatToken(const String &value, float fallback)
  {
    String parsed = value;
    parsed.trim();
    if (parsed.isEmpty())
      return fallback;

    char *end = nullptr;
    const float result = strtof(parsed.c_str(), &end);
    if (end == parsed.c_str())
      return fallback;
    while (*end != '\0' && isspace((unsigned char)*end))
      ++end;
    return *end == '\0' ? result : fallback;
  }

  float normalizeVatPercent(float value)
  {
    if (!isfinite(value))
      return kDefaultVatPercent;
    if (value < 0.0f || value > 100.0f)
      return kDefaultVatPercent;
    return value;
  }

  float normalizeFixedCostPerKwh(float value)
  {
    if (!isfinite(value))
      return kDefaultFixedCostPerKwh;
    if (value < -100.0f || value > 100.0f)
      return kDefaultFixedCostPerKwh;
    return value;
  }

  void normalizeSecrets(AppSecrets &secrets)
  {
    secrets.nordpoolArea =
        normalizeToken(secrets.nordpoolArea, kDefaultNordpoolArea, kAreaMaxLen, kNordpoolAreas, kNordpoolAreaCount);
    secrets.nordpoolCurrency = normalizeToken(
        secrets.nordpoolCurrency,
        kDefaultNordpoolCurrency,
        kCurrencyMaxLen,
        kNordpoolCurrencies,
        kNordpoolCurrencyCount);
    secrets.nordpoolResolutionMinutes = normalizeResolutionMinutes(secrets.nordpoolResolutionMinutes);
    secrets.vatPercent = normalizeVatPercent(secrets.vatPercent);
    secrets.fixedCostPerKwh = normalizeFixedCostPerKwh(secrets.fixedCostPerKwh);
  }

  void saveConfigCallback()
  {
    gSaveConfigRequested = true;
  }

  bool waitForConnection(uint32_t timeoutMs)
  {
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs)
    {
      delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
  }

  void saveSecretsToPrefs(const AppSecrets &secrets)
  {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false))
    {
      logf("Secrets save failed: prefs begin");
      return;
    }
    prefs.putString(kAreaKey, secrets.nordpoolArea);
    prefs.putString(kCurrencyKey, secrets.nordpoolCurrency);
    prefs.putUShort(kResolutionKey, secrets.nordpoolResolutionMinutes);
    prefs.putFloat(kVatPercentKey, secrets.vatPercent);
    prefs.putFloat(kFixedCostPerKwhKey, secrets.fixedCostPerKwh);
    prefs.end();
    logf(
        "Secrets saved: area=%s currency=%s resolution=%u vat=%.2f%% fixed_kwh=%.4f",
        secrets.nordpoolArea.c_str(),
        secrets.nordpoolCurrency.c_str(),
        (unsigned)secrets.nordpoolResolutionMinutes,
        secrets.vatPercent,
        secrets.fixedCostPerKwh);
  }

} // namespace

void loadAppSecrets(AppSecrets &out)
{
  out.nordpoolArea = kDefaultNordpoolArea;
  out.nordpoolCurrency = kDefaultNordpoolCurrency;
  out.nordpoolResolutionMinutes = kDefaultNordpoolResolutionMinutes;
  out.vatPercent = kDefaultVatPercent;
  out.fixedCostPerKwh = kDefaultFixedCostPerKwh;

  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true))
  {
    out.nordpoolArea = prefs.getString(kAreaKey, out.nordpoolArea);
    out.nordpoolCurrency = prefs.getString(kCurrencyKey, out.nordpoolCurrency);
    out.nordpoolResolutionMinutes = prefs.getUShort(kResolutionKey, out.nordpoolResolutionMinutes);
    if (prefs.isKey(kVatPercentKey))
    {
      out.vatPercent = prefs.getFloat(kVatPercentKey, out.vatPercent);
    }
    else if (prefs.isKey(kLegacyPriceMultiplierKey))
    {
      const float legacyMultiplier = prefs.getFloat(kLegacyPriceMultiplierKey, 1.0f + out.vatPercent / 100.0f);
      out.vatPercent = (legacyMultiplier - 1.0f) * 100.0f;
    }

    if (prefs.isKey(kFixedCostPerKwhKey))
    {
      out.fixedCostPerKwh = prefs.getFloat(kFixedCostPerKwhKey, out.fixedCostPerKwh);
    }
    else if (prefs.isKey(kLegacyFixedAddOreKey))
    {
      const float legacyFixedOre = prefs.getFloat(kLegacyFixedAddOreKey, 0.0f);
      out.fixedCostPerKwh = legacyFixedOre / 100.0f;
    }
    prefs.end();
  }

  normalizeSecrets(out);
}

bool wifiConnectWithConfigPortal(AppSecrets &secrets, uint16_t portalTimeoutSeconds)
{
  loadAppSecrets(secrets);
  if (WiFi.status() == WL_CONNECTED)
  {
    return true;
  }

  WiFi.mode(WIFI_STA);

  char areaBuffer[kAreaMaxLen + 1];
  char currencyBuffer[kCurrencyMaxLen + 1];
  char resolutionBuffer[kResolutionMaxLen + 1];
  char vatPercentBuffer[kVatPercentMaxLen + 1];
  char fixedCostPerKwhBuffer[kFixedCostPerKwhMaxLen + 1];
  secrets.nordpoolArea.toCharArray(areaBuffer, sizeof(areaBuffer));
  secrets.nordpoolCurrency.toCharArray(currencyBuffer, sizeof(currencyBuffer));
  snprintf(resolutionBuffer, sizeof(resolutionBuffer), "%u", (unsigned)secrets.nordpoolResolutionMinutes);
  snprintf(vatPercentBuffer, sizeof(vatPercentBuffer), "%.2f", secrets.vatPercent);
  snprintf(fixedCostPerKwhBuffer, sizeof(fixedCostPerKwhBuffer), "%.4f", secrets.fixedCostPerKwh);

  WiFiManager wifiManager;
  WiFiManagerParameter areaParam("NordPoolArea", "Nord Pool area:", areaBuffer, sizeof(areaBuffer));
  WiFiManagerParameter currencyParam("NordPoolCurrency", "currency:", currencyBuffer, sizeof(currencyBuffer));
  WiFiManagerParameter resolutionParam(
      "NordPoolResolution",
      "Resolution (minutes):",
      resolutionBuffer,
      sizeof(resolutionBuffer));
  WiFiManagerParameter vatPercentParam(
      "VatPercent",
      "VAT (%):",
      vatPercentBuffer,
      sizeof(vatPercentBuffer));
  WiFiManagerParameter fixedCostPerKwhParam(
      "FixedCostPerKwh",
      "Total fixed cost / kWh (cents):",
      fixedCostPerKwhBuffer,
      sizeof(fixedCostPerKwhBuffer));

  gSaveConfigRequested = false;
  wifiManager.addParameter(&areaParam);
  wifiManager.addParameter(&currencyParam);
  wifiManager.addParameter(&resolutionParam);
  wifiManager.addParameter(&vatPercentParam);
  wifiManager.addParameter(&fixedCostPerKwhParam);
  wifiManager.setConfigPortalTimeout(portalTimeoutSeconds);
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setCustomHeadElement(kPortalCustomHead);
  wifiManager.setDarkMode(true);

  char apName[32];
  snprintf(apName, sizeof(apName), "ElMeter-%" PRIx64, ESP.getEfuseMac());
  const String apNameString(apName);

  wifiManager.setAPCallback([apNameString, portalTimeoutSeconds](WiFiManager *)
                            { displayDrawWifiConfigPortal(apNameString.c_str(), portalTimeoutSeconds); });
  wifiManager.setConfigPortalTimeoutCallback([portalTimeoutSeconds]()
                                             { displayDrawWifiConfigTimeout(portalTimeoutSeconds); });

  logf("WiFiManager autoConnect start: AP='%s' timeout=%us", apName, (unsigned)portalTimeoutSeconds);
  if (!wifiManager.autoConnect(apName))
  {
    logf("WiFiManager failed or timed out");
    return false;
  }

  if (gSaveConfigRequested)
  {
    secrets.nordpoolArea = String(areaParam.getValue());
    secrets.nordpoolCurrency = String(currencyParam.getValue());
    secrets.nordpoolResolutionMinutes = parseResolutionToken(String(resolutionParam.getValue()));
    secrets.vatPercent = parseFloatToken(String(vatPercentParam.getValue()), secrets.vatPercent);
    secrets.fixedCostPerKwh = parseFloatToken(String(fixedCostPerKwhParam.getValue()), secrets.fixedCostPerKwh);
    normalizeSecrets(secrets);
    saveSecretsToPrefs(secrets);
    gSaveConfigRequested = false;
  }
  else
  {
    normalizeSecrets(secrets);
  }

  logf("WiFi connected: ssid='%s' ip=%s area=%s currency=%s resolution=%u vat=%.2f%% fixed_kwh=%.4f",
       WiFi.SSID().c_str(),
       WiFi.localIP().toString().c_str(),
       secrets.nordpoolArea.c_str(),
       secrets.nordpoolCurrency.c_str(),
       (unsigned)secrets.nordpoolResolutionMinutes,
       secrets.vatPercent,
       secrets.fixedCostPerKwh);
  return true;
}

bool wifiReconnect(uint32_t timeoutMs)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return true;
  }

  const uint32_t now = millis();
  if (now - gLastReconnectAttemptMs < kReconnectCooldownMs)
  {
    return false;
  }
  gLastReconnectAttemptMs = now;

  WiFi.mode(WIFI_STA);
  logf("WiFi reconnect start");
  WiFi.begin();

  if (waitForConnection(timeoutMs))
  {
    logf("WiFi connected: ip=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }

  logf("WiFi reconnect timeout: status=%d", WiFi.status());
  return false;
}

void wifiResetSettings()
{
  WiFiManager wifiManager;
  wifiManager.resetSettings();

  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false))
  {
    prefs.clear();
    prefs.end();
  }
}
