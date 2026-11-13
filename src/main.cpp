#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <DNSServer.h>
#include <ESP32Ping.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <qrcode.h>
#include <SD_MMC.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <WiFiProv.h>
#include "router_tables_data.h"
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_mac.h>
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <lwip/etharp.h>
#include <lwip/opt.h>
#include <lwip/tcpip.h>
#include <sdmmc_cmd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <new>
#include <ctime>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "firmware_version.h"

namespace {

constexpr int PIN_LCD_DC = 41;
constexpr int PIN_LCD_CS = 42;
constexpr int PIN_LCD_SCLK = 40;
constexpr int PIN_LCD_MOSI = 45;
constexpr int PIN_LCD_RST = 39;
constexpr int PIN_LCD_BL = 46;
constexpr int PIN_BOOT = 0;
constexpr int PIN_SD_CLK = SD_CLK;
constexpr int PIN_SD_CMD = SD_CMD;
constexpr int PIN_SD_D0 = SD_D0;
constexpr int SCREEN_WIDTH = 172;
constexpr int SCREEN_HEIGHT = 320;
constexpr uint8_t SCREEN_ROTATION = 0;
constexpr uint8_t MAX_SAVED_NETWORKS = 5;
constexpr size_t MIN_TEST_BYTES = 1024 * 1024;
constexpr size_t MAX_TEST_BYTES = 5 * 1024 * 1024;
constexpr uint32_t TEST_DOWNLOAD_IDLE_TIMEOUT_MS = 15000;
constexpr uint32_t TEST_DOWNLOAD_TOTAL_TIMEOUT_MS = 120000;
constexpr char AP_SSID[] = "ESP32-SiteSurvey";
constexpr char AP_PASSWORD[] = "12345678";
const IPAddress SADP_MULTICAST_IP(239, 255, 255, 250);
constexpr uint16_t SADP_PORT = 37020;
constexpr uint32_t SADP_LISTEN_TIMEOUT_MS = 3000;
constexpr size_t MAX_SADP_DEVICES = 32;
const IPAddress DH_DISCOVER_MULTICAST_IP(239, 255, 255, 251);
constexpr uint16_t DH_DISCOVER_PORT = 37810;
constexpr uint32_t DH_DISCOVER_LISTEN_TIMEOUT_MS = 3000;
constexpr size_t MAX_DH_DISCOVER_DEVICES = 32;
const IPAddress GENERIC_WSD_MULTICAST_IP(239, 255, 255, 250);
constexpr uint16_t GENERIC_WSD_PORT = 3702;
const IPAddress GENERIC_SSDP_MULTICAST_IP(239, 255, 255, 250);
constexpr uint16_t GENERIC_SSDP_PORT = 1900;
const IPAddress GENERIC_MDNS_MULTICAST_IP(224, 0, 0, 251);
constexpr uint16_t GENERIC_MDNS_PORT = 5353;
constexpr uint32_t GENERIC_DISCOVERY_TIMEOUT_MS = 2000;
constexpr size_t MAX_GENERIC_DEVICES = 64;
constexpr char POINTS_FILE[] = "/points.csv";
constexpr char CCTV_ASSETS_FILE[] = "/cctv_assets.csv";
constexpr char CCTV_HISTORY_FILE[] = "/cctv_history.csv";
constexpr char DISCOVERY_HISTORY_FILE[] = "/discovery_history.csv";
constexpr size_t MAX_DISCOVERY_HISTORY_ROWS = 200;
constexpr char ERROR_LOG_FILE[] = "/errors.log";
constexpr char ERROR_LOG_ARCHIVE_FILE[] = "/errors.previous.log";
constexpr size_t ERROR_LOG_MAX_BYTES = 16U * 1024U;
constexpr size_t ERROR_LOG_LINE_MAX = 320;
constexpr size_t PENDING_ERROR_LOG_COUNT = 8;
constexpr char GATEWAY_VENDOR_URL[] = "https://api.macvendors.com/";
constexpr uint32_t GATEWAY_VENDOR_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr char FIRMWARE_VERSION[] = CCTV_FIRMWARE_VERSION;
constexpr uint32_t OTA_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr size_t OTA_MANIFEST_MAX_BYTES = 2048;

Arduino_DataBus *displayBus = new Arduino_ESP32SPI(
    PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *display = new Arduino_ST7789(
    displayBus, PIN_LCD_RST, SCREEN_ROTATION, true, SCREEN_WIDTH, SCREEN_HEIGHT,
    34, 0, 34, 0);

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
IPAddress apAddress(192, 168, 4, 1);
IPAddress apMask(255, 255, 255, 0);

struct SavedNetwork {
  String ssid;
  String password;
};

struct ScannedNetwork {
  String ssid;
  String bssid;
  int32_t rssi;
  uint8_t channel;
  wifi_auth_mode_t auth;
};

struct SurveyPoint {
  uint32_t uptimeMs;
  String name;
  float mbps;
  int32_t rssi;
  uint8_t channel;
  float latencyMs;
};

struct CctvAsset {
  String ip;
  String name;
  String type;
  String mac;
  String manufacturer;
  String model;
  String firmware;
  String ipMode;
};

struct CctvHistoryRecord {
  String date;
  String time;
  CctvAsset asset;
  bool hasRssi;
  int32_t rssi;
};

struct SadpDevice {
  String ip;
  String mac;
  String model;
  String firmware;
};

struct DhDiscoverDevice {
  String ip;
  String mac;
  String model;
  String deviceClass;
  String firmware;
  String serial;
};

struct GenericDiscoveryDevice {
  String ip;
  String protocol;
  String identity;
  String endpoint;
};

struct DiscoveryHistoryRecord {
  String date;
  String time;
  String source;
  String ssid;
  String gatewayIp;
  String ip;
  String protocol;
  String identity;
  String endpoint;
  String mac;
  String model;
  String deviceClass;
  String firmware;
  String serial;
};

struct DiscoveryHistorySaveStatus {
  bool saved;
  size_t savedCount;
  String error;
};

struct ArpSnapshotEntry {
  char ip[16];
  uint8_t mac[6];
};

struct ArpObservation {
  String ip;
  String mac;
  uint32_t lastSeenAt;
};

struct PointAnalysis {
  uint8_t score;
  String quality;
  String recommendation;
  String anomaly;
};

enum class BootQrScreen : uint8_t {
  Status,
  AccessPointPortal,
  WifiProvisioning,
  StaPortal,
  SdCard
};

std::vector<SavedNetwork> savedNetworks;
std::vector<ScannedNetwork> scanResults;
std::vector<CctvAsset> cctvAssets;
std::vector<SadpDevice> sadpDevices;
std::vector<DhDiscoverDevice> dhDiscoverDevices;
std::vector<GenericDiscoveryDevice> genericDiscoveryDevices;
std::vector<ArpObservation> arpObservations;
String arpObservationsNetwork;
ArpSnapshotEntry arpSnapshot[ARP_TABLE_SIZE] = {};
size_t arpSnapshotCount = 0;
bool arpSnapshotReady = false;
SemaphoreHandle_t errorLogMutex = nullptr;
SemaphoreHandle_t gatewayVendorMutex = nullptr;
char pendingErrorLog[PENDING_ERROR_LOG_COUNT][ERROR_LOG_LINE_MAX] = {};
size_t pendingErrorLogCount = 0;
bool serialWasConnected = false;
uint32_t errorLogBootId = 0;
String deviceId = "CCTV-S3-ID-ERROR";
String deviceAlias;
String deviceChipModel = "Desconocido";
String deviceResetReason = "Desconocido";
int deviceResetReasonCode = 0;
uint16_t deviceChipRevision = 0;
uint8_t deviceChipCores = 0;
uint32_t deviceFlashBytes = 0;
uint32_t devicePsramBytes = 0;
uint32_t deviceBootCount = 0;
String connectionMessage = "Esperando red STA";
String lastNetworkError;
String latestPointName;
PointAnalysis latestAnalysis{0, "Sin datos", "Ejecuta una medición para analizar el punto", ""};
SavedNetwork pendingNetwork;
bool rememberPendingNetwork = false;
bool connectionAttemptActive = false;
bool filesystemReady = false;
bool sdCardReady = false;
volatile bool sdFormatRunning = false;
volatile bool sdFormatSucceeded = false;
bool sdFormatAttempted = false;
String sdLastMessage = "Tarjeta no inicializada";
bool preferencesReady = false;
bool accessPointReady = false;
bool displayReady = false;
bool dnsServerReady = false;
String otaManifestUrl;
String otaLatestVersion;
String otaAvailableVersion;
String otaFirmwareUrl;
String otaFirmwareSha256;
size_t otaFirmwareSize = 0;
String otaLastStatus = "Actualizaciones OTA sin configurar";
bool otaCheckRequested = false;
bool otaUpdateRequested = false;
bool otaUpdateInProgress = false;
int otaProgressPercent = 0;
uint32_t nextOtaCheckAt = 0;
uint32_t otaConfigAuthorizationUntil = 0;
bool otaConfigAuthorized = false;
bool bleProvisioningActive = false;
bool bleProvisioningRequested = false;
bool staWasConnected = false;
String gatewayMac;
String gatewayMacIp;
String gatewayMacNetwork;
String gatewayObservedIp;
String gatewayObservedNetwork;
String gatewayArpRequestedIp;
char gatewayArpRequestedNetwork[51] = {};
char arpSnapshotNetwork[sizeof(gatewayArpRequestedNetwork)] = {};
uint32_t nextGatewayArpLookupAt = 0;
uint32_t nextGatewayArpRequestAt = 0;
String gatewayManufacturer;
String gatewayManufacturerStatus = "Esperando MAC del gateway";
String gatewayVendorLookupKey;
String gatewayVendorAttemptedKey;
bool gatewayVendorLookupRunning = false;
bool gatewayVendorLookupResolved = false;
uint32_t gatewayVendorRetryAt = 0;
uint32_t lastGatewayPingAt = 0;
bool gatewayPingAttempted = false;
uint32_t lastDevicePingAt = 0;
bool devicePingAttempted = false;
uint32_t devicePingWindowStartedAt = 0;
uint8_t devicePingAttemptsInWindow = 0;
bool devicePingWindowStarted = false;
uint32_t lastHttpProbeAt = 0;
bool httpProbeAttempted = false;
uint32_t httpProbeWindowStartedAt = 0;
uint8_t httpProbeAttemptsInWindow = 0;
bool httpProbeWindowStarted = false;
bool gatewayArpCallbackPending = false;
bool gatewayArpResultReady = false;
bool gatewayArpShouldRequest = false;
uint8_t gatewayArpResultMac[6] = {};
int32_t gatewayArpRequestError = 0;
bool gatewayArpNetifError = false;
ip4_addr_t gatewayArpTargetIp = {};
portMUX_TYPE gatewayArpMux = portMUX_INITIALIZER_UNLOCKED;
BootQrScreen bootQrScreen = BootQrScreen::Status;
char bleServiceName[20] = "Prov_SiteSurvey";
char bleProofOfPossession[9] = "S3LCD147";
portMUX_TYPE bleEventMux = portMUX_INITIALIZER_UNLOCKED;
bool bleStartPending = false;
bool bleSuccessPending = false;
bool bleFailurePending = false;
bool bleAuthFailurePending = false;
bool bleCredentialsPending = false;
char bleReceivedSsid[33] = {};
char bleReceivedPassword[65] = {};
uint32_t connectionAttemptAt = 0;
uint32_t nextReconnectAt = 0;
uint32_t nextScreenRefreshAt = 0;
size_t reconnectIndex = 0;

void startBleProvisioning(bool force);

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
    case '"':
      escaped += F("\\\"");
      break;
    case '\\':
      escaped += F("\\\\");
      break;
    case '\b':
      escaped += F("\\b");
      break;
    case '\f':
      escaped += F("\\f");
      break;
    case '\n':
      escaped += F("\\n");
      break;
    case '\r':
      escaped += F("\\r");
      break;
    case '\t':
      escaped += F("\\t");
      break;
    default:
      if (static_cast<uint8_t>(c) < 0x20) {
        escaped += ' ';
      } else {
        escaped += c;
      }
    }
  }
  return escaped;
}

String csvEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 4);
  bool quote = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '"') {
      escaped += F("\"\"");
      quote = true;
    } else {
      if (c == ',' || c == '\n' || c == '\r') {
        quote = true;
      }
      escaped += c;
    }
  }
  if (quote) {
    escaped = String("\"") + escaped + '"';
  }
  return escaped;
}

void sendJson(int statusCode, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(statusCode, "application/json; charset=utf-8", body);
}

bool appendErrorLineLocked(const String &line) {
  if (!filesystemReady) {
    return false;
  }

  size_t currentSize = 0;
  if (LittleFS.exists(ERROR_LOG_FILE)) {
    File currentFile = LittleFS.open(ERROR_LOG_FILE, FILE_READ);
    if (!currentFile) {
      Serial.println(F("ERROR LOG: no se pudo consultar el tamaño"));
      return false;
    }
    currentSize = currentFile.size();
    currentFile.close();
  }

  if (currentSize + line.length() > ERROR_LOG_MAX_BYTES) {
    if (LittleFS.exists(ERROR_LOG_ARCHIVE_FILE) &&
        !LittleFS.remove(ERROR_LOG_ARCHIVE_FILE)) {
      Serial.println(F("ERROR LOG: no se pudo reemplazar el archivo anterior"));
      return false;
    }
    if (LittleFS.exists(ERROR_LOG_FILE) &&
        !LittleFS.rename(ERROR_LOG_FILE, ERROR_LOG_ARCHIVE_FILE)) {
      Serial.println(F("ERROR LOG: no se pudo rotar el archivo actual"));
      return false;
    }
  }

  File logFile = LittleFS.open(ERROR_LOG_FILE, FILE_APPEND);
  if (!logFile) {
    Serial.println(F("ERROR LOG: no se pudo abrir para escritura"));
    return false;
  }
  const size_t written = logFile.print(line);
  logFile.flush();
  logFile.close();
  if (written != line.length()) {
    Serial.println(F("ERROR LOG: escritura incompleta"));
    return false;
  }
  return true;
}

void recordError(const char *source, const String &message) {
  String cleanMessage = message;
  if (cleanMessage.length() > 180) {
    cleanMessage.remove(180);
  }
  for (size_t i = 0; i < cleanMessage.length(); ++i) {
    const uint8_t character = static_cast<uint8_t>(cleanMessage[i]);
    if (character < 0x20 || character == 0x7f) {
      cleanMessage[i] = ' ';
    }
  }

  String line;
  line.reserve(ERROR_LOG_LINE_MAX);
  line += F("[boot ");
  line += String(errorLogBootId, HEX);
  line += F(" | ");
  line += String(millis());
  line += F(" ms] [device ");
  line += deviceId;
  line += F(" | alias ");
  if (deviceAlias.isEmpty()) {
    line += F("sin-asignar");
  } else {
    line += deviceAlias;
  }
  line += F("] [fw ");
  line += FIRMWARE_VERSION;
  line += F("] [");
  line += source;
  line += F("] ");
  line += cleanMessage;
  line += F("\r\n");
  if (line.length() > ERROR_LOG_LINE_MAX - 1) {
    line.remove(ERROR_LOG_LINE_MAX - 1);
    line += '\n';
  }

  if (errorLogMutex == nullptr ||
      xSemaphoreTake(errorLogMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    Serial.println(F("ERROR LOG: no se pudo bloquear el registro"));
    return;
  }

  bool stored = false;
  if (filesystemReady) {
    stored = appendErrorLineLocked(line);
  } else {
    if (pendingErrorLogCount == PENDING_ERROR_LOG_COUNT) {
      memmove(pendingErrorLog[0], pendingErrorLog[1],
              (PENDING_ERROR_LOG_COUNT - 1) * ERROR_LOG_LINE_MAX);
      --pendingErrorLogCount;
    }
    line.toCharArray(pendingErrorLog[pendingErrorLogCount],
                     ERROR_LOG_LINE_MAX);
    ++pendingErrorLogCount;
    stored = true;
  }
  xSemaphoreGive(errorLogMutex);
  if (!stored) {
    Serial.printf("ERROR LOG: no se pudo guardar [%s] %s\n", source,
                  cleanMessage.c_str());
  }
}

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
  case ESP_RST_POWERON:
    return "PowerOn";
  case ESP_RST_EXT:
    return "External";
  case ESP_RST_SW:
    return "Software";
  case ESP_RST_PANIC:
    return "Panic";
  case ESP_RST_INT_WDT:
    return "InterruptWatchdog";
  case ESP_RST_TASK_WDT:
    return "TaskWatchdog";
  case ESP_RST_WDT:
    return "Watchdog";
  case ESP_RST_DEEPSLEEP:
    return "DeepSleep";
  case ESP_RST_BROWNOUT:
    return "Brownout";
  case ESP_RST_SDIO:
    return "SDIO";
  case ESP_RST_USB:
    return "USB";
  case ESP_RST_JTAG:
    return "JTAG";
  case ESP_RST_EFUSE:
    return "eFuse";
  case ESP_RST_PWR_GLITCH:
    return "PowerGlitch";
  case ESP_RST_CPU_LOCKUP:
    return "CpuLockup";
  default:
    return "Unknown";
  }
}

void initializeDeviceIdentity() {
  uint8_t baseMac[6] = {};
  if (esp_efuse_mac_get_default(baseMac) == ESP_OK) {
    char hardwareId[24] = {};
    snprintf(hardwareId, sizeof(hardwareId),
             "CCTV-S3-%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1],
             baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
    deviceId = hardwareId;
  } else {
    recordError("Identidad", "No se pudo leer la MAC de fábrica del eFuse");
  }
  deviceChipModel = ESP.getChipModel();
  deviceChipRevision = ESP.getChipRevision();
  deviceChipCores = ESP.getChipCores();
  deviceFlashBytes = ESP.getFlashChipSize();
  devicePsramBytes = ESP.getPsramSize();
  const esp_reset_reason_t resetReason = esp_reset_reason();
  deviceResetReason = resetReasonName(resetReason);
  deviceResetReasonCode = static_cast<int>(resetReason);
}

void recordDeviceBootInfo() {
  String line;
  line.reserve(220);
  line += F("[device id=");
  line += deviceId;
  line += F(" alias=");
  if (deviceAlias.isEmpty()) {
    line += F("sin-asignar");
  } else {
    line += deviceAlias;
  }
  line += F(" firmware=");
  line += FIRMWARE_VERSION;
  line += F(" chip=");
  line += deviceChipModel;
  line += F(" revision=");
  line += String(deviceChipRevision);
  line += F(" cores=");
  line += String(deviceChipCores);
  line += F(" flash_bytes=");
  line += String(deviceFlashBytes);
  line += F(" psram_bytes=");
  line += String(devicePsramBytes);
  line += F(" boot_count=");
  line += String(deviceBootCount);
  line += F(" reset=");
  line += deviceResetReason;
  line += '(';
  line += String(deviceResetReasonCode);
  line += ')';
  line += F(" boot_id=");
  line += String(errorLogBootId, HEX);
  line += F("]\r\n");

  if (errorLogMutex == nullptr ||
      xSemaphoreTake(errorLogMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    Serial.print(line);
    return;
  }
  const bool stored = appendErrorLineLocked(line);
  xSemaphoreGive(errorLogMutex);
  if (!stored) {
    Serial.print(line);
  }
}

String deviceInfoJson() {
  String body = F("{\"id\":\"");
  body += jsonEscape(deviceId);
  body += F("\",\"alias\":\"");
  body += jsonEscape(deviceAlias);
  body += F("\",\"firmwareVersion\":\"");
  body += FIRMWARE_VERSION;
  body += F("\",\"chipModel\":\"");
  body += jsonEscape(deviceChipModel);
  body += F("\",\"chipRevision\":");
  body += String(deviceChipRevision);
  body += F(",\"chipCores\":");
  body += String(deviceChipCores);
  body += F(",\"flashBytes\":");
  body += String(deviceFlashBytes);
  body += F(",\"psramBytes\":");
  body += String(devicePsramBytes);
  body += F(",\"bootCount\":");
  body += String(deviceBootCount);
  body += F(",\"resetReasonCode\":");
  body += String(deviceResetReasonCode);
  body += F(",\"resetReason\":\"");
  body += jsonEscape(deviceResetReason);
  body += F("\",\"bootId\":\"");
  body += String(errorLogBootId, HEX);
  body += F("\"}");
  return body;
}

void recordDeviceAliasEvent() {
  String line = F("[device id=");
  line += deviceId;
  line += F(" alias=");
  line += deviceAlias;
  line += F(" event=alias-assigned boot_id=");
  line += String(errorLogBootId, HEX);
  line += F("]\r\n");
  if (errorLogMutex == nullptr ||
      xSemaphoreTake(errorLogMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    Serial.print(line);
    return;
  }
  const bool stored = appendErrorLineLocked(line);
  xSemaphoreGive(errorLogMutex);
  if (!stored) {
    Serial.print(line);
  }
}

void flushPendingErrorLog() {
  if (!filesystemReady || errorLogMutex == nullptr ||
      xSemaphoreTake(errorLogMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return;
  }

  size_t storedCount = 0;
  while (storedCount < pendingErrorLogCount &&
         appendErrorLineLocked(String(pendingErrorLog[storedCount]))) {
    ++storedCount;
  }
  if (storedCount > 0) {
    const size_t remainingCount = pendingErrorLogCount - storedCount;
    if (remainingCount > 0) {
      memmove(pendingErrorLog[0], pendingErrorLog[storedCount],
              remainingCount * ERROR_LOG_LINE_MAX);
    }
    pendingErrorLogCount = remainingCount;
  }
  xSemaphoreGive(errorLogMutex);
}

bool isIeeeErrorLogLine(const String &line) {
  String normalized = line;
  normalized.toLowerCase();
  return normalized.indexOf("ieee") >= 0 || normalized.indexOf("oui") >= 0;
}

bool appendErrorLogFile(String &content, const char *path, bool includeIeee) {
  if (!LittleFS.exists(path)) {
    return true;
  }
  File logFile = LittleFS.open(path, FILE_READ);
  if (!logFile) {
    return false;
  }

  String line;
  line.reserve(ERROR_LOG_LINE_MAX);
  while (logFile.available() && content.length() < ERROR_LOG_MAX_BYTES * 2) {
    const int character = logFile.read();
    if (character < 0) {
      continue;
    }
    if (character == '\n') {
      if (includeIeee || !isIeeeErrorLogLine(line)) {
        content += line;
        content += '\n';
      }
      line = "";
    } else if (line.length() < ERROR_LOG_LINE_MAX) {
      line += static_cast<char>(character);
    }
  }
  if (!line.isEmpty() && (includeIeee || !isIeeeErrorLogLine(line))) {
    content += line;
  }
  logFile.close();
  return true;
}

bool readErrorLog(String &content, bool includeIeee = true) {
  if (errorLogMutex == nullptr ||
      xSemaphoreTake(errorLogMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }

  content = "";
  content.reserve(ERROR_LOG_MAX_BYTES * 2);
  if (filesystemReady) {
    const char *paths[] = {ERROR_LOG_ARCHIVE_FILE, ERROR_LOG_FILE};
    for (const char *path : paths) {
      if (!appendErrorLogFile(content, path, includeIeee)) {
        xSemaphoreGive(errorLogMutex);
        return false;
      }
    }
  } else {
    content = F("LittleFS no disponible; este registro solo existe en RAM.\r\n");
  }
  for (size_t i = 0; i < pendingErrorLogCount &&
                     content.length() < ERROR_LOG_MAX_BYTES * 2;
       ++i) {
    const String line(pendingErrorLog[i]);
    if (includeIeee || !isIeeeErrorLogLine(line)) {
      content += line;
    }
  }
  if (content.isEmpty() && !filesystemReady) {
    content = F("LittleFS no disponible; no se puede persistir el log.\r\n");
  } else if (content.isEmpty()) {
    content = F("Sin errores registrados.\r\n");
  }
  xSemaphoreGive(errorLogMutex);
  return true;
}

void dumpErrorLogToSerial() {
  String content;
  Serial.println(F("=== ESP32 ERROR LOG: INICIO ==="));
  Serial.print(F("DEVICE_INFO "));
  Serial.println(deviceInfoJson());
  if (!readErrorLog(content)) {
    Serial.println(F("No se pudo leer el log; comprueba LittleFS."));
  } else if (content == F("Sin errores registrados.\r\n")) {
    Serial.println(F("Sin errores registrados."));
  } else {
    Serial.print(content);
  }
  Serial.println(F("=== ESP32 ERROR LOG: FIN ==="));
  Serial.flush();
}

void sendError(int statusCode, const String &message) {
  if (statusCode >= 500) {
    recordError("API", String(statusCode) + ": " + message);
  }
  String body = F("{\"error\":\"");
  body += jsonEscape(message);
  body += F("\"}");
  sendJson(statusCode, body);
}

bool requireSession() {
  // El portal queda abierto por solicitud; cualquier cliente que lo alcance
  // puede consultar y modificar sus datos. Durante una actualización OTA solo
  // se atiende el estado para no interferir con la descarga.
  if (otaUpdateInProgress && server.uri() != "/api/ota/status") {
    sendJson(503, F("{\"error\":\"Actualización en curso; espera a que el equipo reinicie\"}"));
    return false;
  }
  return true;
}

String encryptionLabel(wifi_auth_mode_t auth) {
  switch (auth) {
  case WIFI_AUTH_OPEN:
    return F("Abierta");
  case WIFI_AUTH_WEP:
    return F("WEP");
  case WIFI_AUTH_WPA_PSK:
    return F("WPA");
  case WIFI_AUTH_WPA2_PSK:
    return F("WPA2");
  case WIFI_AUTH_WPA_WPA2_PSK:
    return F("WPA/WPA2");
  case WIFI_AUTH_WPA3_PSK:
    return F("WPA3");
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return F("WPA2/WPA3");
  default:
    return F("Otro");
  }
}

String riskLabel(wifi_auth_mode_t auth) {
  if (auth == WIFI_AUTH_OPEN || auth == WIFI_AUTH_WEP) {
    return F("Vulnerable / Riesgo alto");
  }
  if (auth == WIFI_AUTH_WPA3_PSK) {
    return F("Protegido (WPA3)");
  }
  if (auth == WIFI_AUTH_WPA2_WPA3_PSK) {
    return F("Modo mixto / Revisar");
  }
  return F("Estándar / Revisar WPS");
}

String scanResultsJson() {
  String body;
  body.reserve(256 + scanResults.size() * 180);
  body = '[';
  for (size_t i = 0; i < scanResults.size(); ++i) {
    const ScannedNetwork &network = scanResults[i];
    if (i != 0) {
      body += ',';
    }
    body += F("{\"ssid\":\"");
    body += jsonEscape(network.ssid);
    body += F("\",\"bssid\":\"");
    body += jsonEscape(network.bssid);
    body += F("\",\"rssi\":");
    body += String(network.rssi);
    body += F(",\"channel\":");
    body += String(network.channel);
    body += F(",\"encryption\":\"");
    body += encryptionLabel(network.auth);
    body += F("\",\"risk\":\"");
    body += riskLabel(network.auth);
    body += F("\"}");
  }
  body += ']';
  return body;
}

bool persistSavedNetworks() {
  if (!preferencesReady) {
    return false;
  }

  for (size_t i = 0; i < savedNetworks.size(); ++i) {
    const String ssidKey = "s" + String(i);
    const String passwordKey = "p" + String(i);
    const bool hasPassword = !savedNetworks[i].password.isEmpty();
    if (preferences.putString(ssidKey.c_str(), savedNetworks[i].ssid) == 0 ||
        (hasPassword &&
         preferences.putString(passwordKey.c_str(),
                               savedNetworks[i].password) == 0)) {
      Serial.println(F("ERROR: no se pudo persistir la lista Wi-Fi en NVS"));
      return false;
    }
    if (!hasPassword) {
      preferences.remove(passwordKey.c_str());
    }
  }

  for (size_t i = savedNetworks.size(); i < MAX_SAVED_NETWORKS; ++i) {
    const String ssidKey = "s" + String(i);
    const String passwordKey = "p" + String(i);
    preferences.remove(ssidKey.c_str());
    preferences.remove(passwordKey.c_str());
  }
  return true;
}

void loadSavedNetworks() {
  savedNetworks.clear();
  if (!preferencesReady) {
    return;
  }

  for (uint8_t i = 0; i < MAX_SAVED_NETWORKS; ++i) {
    const String ssidKey = "s" + String(i);
    const String passwordKey = "p" + String(i);
    const String ssid = preferences.getString(ssidKey.c_str(), "");
    if (ssid.isEmpty()) {
      continue;
    }
    savedNetworks.push_back(
        {ssid, preferences.getString(passwordKey.c_str(), "")});
  }
}

bool rememberNetwork(const SavedNetwork &network) {
  savedNetworks.erase(
      std::remove_if(savedNetworks.begin(), savedNetworks.end(),
                     [&network](const SavedNetwork &saved) {
                       return saved.ssid == network.ssid;
                     }),
      savedNetworks.end());
  savedNetworks.insert(savedNetworks.begin(), network);
  if (savedNetworks.size() > MAX_SAVED_NETWORKS) {
    savedNetworks.resize(MAX_SAVED_NETWORKS);
  }
  return persistSavedNetworks();
}

String formatMacAddress(const uint8_t mac[6]) {
  char formatted[18];
  snprintf(formatted, sizeof(formatted), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(formatted);
}

struct GatewayVendorTaskContext {
  String key;
  String url;
};

void resetGatewayVendorLookup() {
  if (gatewayVendorMutex == nullptr ||
      xSemaphoreTake(gatewayVendorMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return;
  }
  gatewayVendorLookupKey = "";
  gatewayVendorAttemptedKey = "";
  gatewayManufacturer = "";
  gatewayManufacturerStatus = "Esperando MAC del gateway";
  gatewayVendorLookupResolved = false;
  gatewayVendorRetryAt = 0;
  xSemaphoreGive(gatewayVendorMutex);
}

void gatewayVendorLookupTask(void *parameter) {
  auto *context = static_cast<GatewayVendorTaskContext *>(parameter);
  String manufacturer;
  String status;
  bool resolved = false;
  NetworkClientSecure tlsClient;
  tlsClient.setTimeout(10000);
  tlsClient.setHandshakeTimeout(10);
  tlsClient.useBuiltinCACertBundle();
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (WiFi.status() != WL_CONNECTED) {
    status = "Sin conexión a Internet";
  } else if (!http.begin(tlsClient, context->url)) {
    status = "No se pudo iniciar la consulta HTTPS";
  } else {
    const int responseCode = http.GET();
    if (responseCode == HTTP_CODE_OK) {
      manufacturer = http.getString();
      manufacturer.trim();
      for (size_t i = 0; i < manufacturer.length(); ++i) {
        if (static_cast<uint8_t>(manufacturer[i]) < 0x20) {
          manufacturer.setCharAt(i, ' ');
        }
      }
      manufacturer.trim();
      if (manufacturer.length() > 120) {
        manufacturer.remove(120);
      }
      if (manufacturer.isEmpty()) {
        status = "El servicio devolvió un resultado vacío";
      } else {
        status = "Fabricante resuelto por api.macvendors.com";
        resolved = true;
      }
    } else if (responseCode == HTTP_CODE_NOT_FOUND) {
      status = "Fabricante no encontrado para esta MAC";
    } else {
      status = "Consulta fallida (HTTP " + String(responseCode) + ")";
    }
    http.end();
  }
  tlsClient.stop();

  if (!resolved) {
    recordError("MAC del gateway", status);
  }
  if (gatewayVendorMutex != nullptr &&
      xSemaphoreTake(gatewayVendorMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    gatewayVendorLookupRunning = false;
    if (gatewayVendorLookupKey == context->key) {
      gatewayManufacturer = manufacturer;
      gatewayManufacturerStatus = status;
      gatewayVendorLookupResolved = resolved;
      gatewayVendorRetryAt = resolved ? 0 : millis() + GATEWAY_VENDOR_RETRY_MS;
    }
    xSemaphoreGive(gatewayVendorMutex);
  }
  delete context;
  vTaskDelete(nullptr);
}

void maintainGatewayVendorLookup() {
  if (WiFi.status() != WL_CONNECTED || gatewayMac.isEmpty() ||
      gatewayMacIp != WiFi.gatewayIP().toString() ||
      gatewayMacNetwork != WiFi.SSID() + "|" + WiFi.BSSIDstr() ||
      gatewayVendorMutex == nullptr) {
    return;
  }

  const String key = gatewayMacNetwork + "|" + gatewayMacIp + "|" + gatewayMac;
  if (xSemaphoreTake(gatewayVendorMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }
  if (gatewayVendorLookupKey != key) {
    gatewayVendorLookupKey = key;
    gatewayVendorAttemptedKey = "";
    gatewayManufacturer = "";
    gatewayManufacturerStatus = "Consulta pendiente";
    gatewayVendorLookupResolved = false;
    gatewayVendorRetryAt = 0;
  }
  const uint32_t now = millis();
  if (gatewayVendorLookupRunning || gatewayVendorLookupResolved ||
      (gatewayVendorAttemptedKey == key &&
       static_cast<int32_t>(now - gatewayVendorRetryAt) < 0)) {
    xSemaphoreGive(gatewayVendorMutex);
    return;
  }
  gatewayVendorLookupRunning = true;
  gatewayVendorAttemptedKey = key;
  gatewayManufacturerStatus = "Consultando api.macvendors.com";
  xSemaphoreGive(gatewayVendorMutex);

  auto *context = new (std::nothrow) GatewayVendorTaskContext{
      key, String(GATEWAY_VENDOR_URL) + gatewayMac};
  if (context == nullptr) {
    if (xSemaphoreTake(gatewayVendorMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      gatewayVendorLookupRunning = false;
      gatewayManufacturerStatus = "Memoria insuficiente para la consulta";
      gatewayVendorRetryAt = millis() + GATEWAY_VENDOR_RETRY_MS;
      xSemaphoreGive(gatewayVendorMutex);
    }
    recordError("MAC del gateway", "No se pudo reservar memoria para la consulta");
    return;
  }

  if (xTaskCreatePinnedToCore(gatewayVendorLookupTask, "gateway-vendor", 8192,
                              context, 1, nullptr, 1) != pdPASS) {
    if (xSemaphoreTake(gatewayVendorMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      gatewayVendorLookupRunning = false;
      gatewayManufacturerStatus = "No se pudo iniciar la consulta";
      gatewayVendorRetryAt = millis() + GATEWAY_VENDOR_RETRY_MS;
      xSemaphoreGive(gatewayVendorMutex);
    }
    recordError("MAC del gateway", "No se pudo iniciar la tarea HTTPS");
    delete context;
  }
}

void gatewayArpLookupCallback(void *) {
  bool found = false;
  bool netifError = false;
  int32_t requestError = 0;
  uint8_t mac[6] = {};
  ArpSnapshotEntry snapshot[ARP_TABLE_SIZE] = {};
  size_t snapshotCount = 0;
  esp_netif_t *staNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  struct netif *lwipNetif =
      staNetif != nullptr
          ? static_cast<struct netif *>(esp_netif_get_netif_impl(staNetif))
          : nullptr;

  if (lwipNetif == nullptr) {
    netifError = true;
  } else {
    for (size_t i = 0; i < ARP_TABLE_SIZE; ++i) {
      ip4_addr_t *entryIp = nullptr;
      struct netif *entryNetif = nullptr;
      struct eth_addr *entryMac = nullptr;
      if (etharp_get_entry(i, &entryIp, &entryNetif, &entryMac) != 1 ||
          entryIp == nullptr || entryMac == nullptr ||
          entryNetif != lwipNetif) {
        continue;
      }
      ip4addr_ntoa_r(entryIp, snapshot[snapshotCount].ip,
                     sizeof(snapshot[snapshotCount].ip));
      for (size_t byte = 0; byte < sizeof(snapshot[snapshotCount].mac); ++byte) {
        snapshot[snapshotCount].mac[byte] = entryMac->addr[byte];
      }
      ++snapshotCount;
    }

    struct eth_addr *ethernetAddress = nullptr;
    const ip4_addr_t *resolvedAddress = nullptr;
    if (etharp_find_addr(lwipNetif, &gatewayArpTargetIp, &ethernetAddress,
                         &resolvedAddress) >= 0 &&
        ethernetAddress != nullptr) {
      for (size_t i = 0; i < sizeof(mac); ++i) {
        mac[i] = ethernetAddress->addr[i];
      }
      found = true;
    } else if (gatewayArpShouldRequest) {
      requestError = static_cast<int32_t>(
          etharp_request(lwipNetif, &gatewayArpTargetIp));
    }
  }

  portENTER_CRITICAL(&gatewayArpMux);
  gatewayArpNetifError = netifError;
  gatewayArpRequestError = requestError;
  for (size_t i = 0; i < snapshotCount; ++i) {
    arpSnapshot[i] = snapshot[i];
  }
  memcpy(arpSnapshotNetwork, gatewayArpRequestedNetwork,
         sizeof(arpSnapshotNetwork));
  arpSnapshotCount = snapshotCount;
  arpSnapshotReady = true;
  if (found) {
    for (size_t i = 0; i < sizeof(mac); ++i) {
      gatewayArpResultMac[i] = mac[i];
    }
    gatewayArpResultReady = true;
  }
  gatewayArpCallbackPending = false;
  portEXIT_CRITICAL(&gatewayArpMux);
}

void maintainGatewayMac() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!gatewayObservedIp.isEmpty()) {
      resetGatewayVendorLookup();
    }
    gatewayObservedIp = "";
    gatewayObservedNetwork = "";
    nextGatewayArpLookupAt = 0;
    nextGatewayArpRequestAt = 0;
    return;
  }

  const IPAddress gateway = WiFi.gatewayIP();
  const String currentGatewayIp = gateway.toString();
  const String currentGatewayNetwork = WiFi.SSID() + "|" + WiFi.BSSIDstr();
  if (currentGatewayIp == "0.0.0.0") {
    return;
  }
  if (currentGatewayIp != gatewayObservedIp ||
      currentGatewayNetwork != gatewayObservedNetwork) {
    resetGatewayVendorLookup();
    gatewayObservedIp = currentGatewayIp;
    gatewayObservedNetwork = currentGatewayNetwork;
    nextGatewayArpLookupAt = millis();
    nextGatewayArpRequestAt = millis();
    if (!gatewayMac.isEmpty() &&
        (gatewayMacIp != currentGatewayIp ||
         gatewayMacNetwork != currentGatewayNetwork)) {
      gatewayMac = "";
      gatewayMacIp = "";
      gatewayMacNetwork = "";
      if (preferencesReady) {
        preferences.remove("gwmac");
        preferences.remove("gwip");
        preferences.remove("gwnet");
      }
    }
  }

  bool resultReady = false;
  bool netifError = false;
  int32_t requestError = 0;
  uint8_t resultMac[6] = {};
  portENTER_CRITICAL(&gatewayArpMux);
  resultReady = gatewayArpResultReady;
  if (resultReady) {
    for (size_t i = 0; i < sizeof(resultMac); ++i) {
      resultMac[i] = gatewayArpResultMac[i];
    }
    gatewayArpResultReady = false;
  }
  netifError = gatewayArpNetifError;
  gatewayArpNetifError = false;
  requestError = gatewayArpRequestError;
  gatewayArpRequestError = 0;
  const bool callbackPending = gatewayArpCallbackPending;
  portEXIT_CRITICAL(&gatewayArpMux);

  if (netifError) {
    Serial.println(F("Gateway ARP: no se encontró la interfaz Wi-Fi STA de lwIP"));
  }
  if (requestError != 0) {
    Serial.printf("Gateway ARP: solicitud ARP falló (%ld)\n",
                  static_cast<long>(requestError));
  }
  if (resultReady && gatewayArpRequestedIp == currentGatewayIp &&
      currentGatewayNetwork == gatewayArpRequestedNetwork) {
    const String resolvedMac = formatMacAddress(resultMac);
    const bool changed = gatewayMac != resolvedMac ||
                         gatewayMacIp != currentGatewayIp ||
                         gatewayMacNetwork != currentGatewayNetwork;
    gatewayMac = resolvedMac;
    gatewayMacIp = currentGatewayIp;
    gatewayMacNetwork = currentGatewayNetwork;
    nextGatewayArpRequestAt = millis() + 300000UL;
    if (changed) {
      Serial.printf("Gateway ARP: MAC %s asociada a %s\n",
                    gatewayMac.c_str(), gatewayMacIp.c_str());
      if (!preferencesReady) {
        Serial.println(F("Gateway ARP: MAC obtenida, pero NVS no está disponible"));
      } else if (preferences.putString("gwmac", gatewayMac) == 0 ||
                 preferences.putString("gwip", gatewayMacIp) == 0 ||
                 preferences.putString("gwnet", gatewayMacNetwork) == 0) {
        Serial.println(F("Gateway ARP: no se pudo guardar la MAC en NVS"));
      }
    }
  }

  if (callbackPending || millis() < nextGatewayArpLookupAt) {
    return;
  }

  IP4_ADDR(&gatewayArpTargetIp, gateway[0], gateway[1], gateway[2],
           gateway[3]);
  gatewayArpRequestedIp = currentGatewayIp;
  currentGatewayNetwork.toCharArray(gatewayArpRequestedNetwork,
                                    sizeof(gatewayArpRequestedNetwork));
  const uint32_t now = millis();
  gatewayArpShouldRequest = now >= nextGatewayArpRequestAt;
  if (gatewayArpShouldRequest) {
    nextGatewayArpRequestAt = now + (gatewayMac.isEmpty() ? 10000UL : 300000UL);
  }
  portENTER_CRITICAL(&gatewayArpMux);
  gatewayArpCallbackPending = true;
  portEXIT_CRITICAL(&gatewayArpMux);
  const err_t callbackStatus = tcpip_callback(gatewayArpLookupCallback, nullptr);
  if (callbackStatus != ERR_OK) {
    portENTER_CRITICAL(&gatewayArpMux);
    gatewayArpCallbackPending = false;
    portEXIT_CRITICAL(&gatewayArpMux);
    Serial.printf("Gateway ARP: no se pudo consultar la caché (%d)\n",
                  static_cast<int>(callbackStatus));
    nextGatewayArpLookupAt = millis() + 2000UL;
    return;
  }
  nextGatewayArpLookupAt = millis() + 2000UL;
}

void maintainArpObservations() {
  if (WiFi.status() != WL_CONNECTED) {
    arpObservations.clear();
    arpObservationsNetwork = "";
    portENTER_CRITICAL(&gatewayArpMux);
    arpSnapshotReady = false;
    arpSnapshotCount = 0;
    portEXIT_CRITICAL(&gatewayArpMux);
    return;
  }

  const String currentNetwork = WiFi.SSID() + "|" + WiFi.BSSIDstr();
  if (arpObservationsNetwork != currentNetwork) {
    arpObservations.clear();
    arpObservationsNetwork = currentNetwork;
  }
  const uint32_t now = millis();
  arpObservations.erase(
      std::remove_if(arpObservations.begin(), arpObservations.end(),
                     [now](const ArpObservation &entry) {
                       return now - entry.lastSeenAt > 600000UL;
                     }),
      arpObservations.end());

  ArpSnapshotEntry snapshot[ARP_TABLE_SIZE] = {};
  char snapshotNetwork[sizeof(arpSnapshotNetwork)] = {};
  size_t snapshotCount = 0;
  portENTER_CRITICAL(&gatewayArpMux);
  if (arpSnapshotReady) {
    snapshotCount = arpSnapshotCount;
    memcpy(snapshotNetwork, arpSnapshotNetwork, sizeof(snapshotNetwork));
    for (size_t i = 0; i < snapshotCount; ++i) {
      snapshot[i] = arpSnapshot[i];
    }
    arpSnapshotReady = false;
  }
  portEXIT_CRITICAL(&gatewayArpMux);
  if (snapshotCount == 0 || currentNetwork != snapshotNetwork) {
    return;
  }

  constexpr size_t MAX_ARP_OBSERVATIONS = 64;
  for (size_t i = 0; i < snapshotCount; ++i) {
    const String ip = snapshot[i].ip;
    if (ip.isEmpty()) {
      continue;
    }
    const String mac = formatMacAddress(snapshot[i].mac);
    auto existing = std::find_if(
        arpObservations.begin(), arpObservations.end(),
        [&ip](const ArpObservation &entry) { return entry.ip == ip; });
    if (existing != arpObservations.end()) {
      existing->mac = mac;
      existing->lastSeenAt = now;
      continue;
    }
    if (arpObservations.size() >= MAX_ARP_OBSERVATIONS) {
      const auto oldest = std::min_element(
          arpObservations.begin(), arpObservations.end(),
          [now](const ArpObservation &left, const ArpObservation &right) {
            return now - left.lastSeenAt > now - right.lastSeenAt;
          });
      if (oldest != arpObservations.end()) {
        arpObservations.erase(oldest);
      }
    }
    arpObservations.push_back({ip, mac, now});
  }
}

bool scanNearbyNetworks();

void startStaConnection(const SavedNetwork &network, bool rememberOnSuccess) {
  pendingNetwork = network;
  rememberPendingNetwork = rememberOnSuccess;
  connectionAttemptActive = true;
  connectionAttemptAt = millis();
  connectionMessage = "Conectando a " + network.ssid;
  lastNetworkError = "";
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  delay(40);
  WiFi.begin(network.ssid.c_str(), network.password.c_str());
}

void maintainStaConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!staWasConnected) {
      staWasConnected = true;
    }
    maintainGatewayMac();
    maintainGatewayVendorLookup();
    if (connectionAttemptActive && rememberPendingNetwork) {
      if (rememberNetwork(pendingNetwork)) {
        Serial.println(F("Red conectada y guardada en la lista blanca NVS"));
      } else {
        lastNetworkError = "No se pudo guardar la red en NVS";
        recordError("Wi-Fi", lastNetworkError);
      }
      rememberPendingNetwork = false;
    }
    connectionAttemptActive = false;
    connectionMessage = "Conectado a " + WiFi.SSID();
    return;
  }

  if (staWasConnected) {
    resetGatewayVendorLookup();
  }
  staWasConnected = false;
  gatewayObservedIp = "";
  nextGatewayArpLookupAt = 0;
  if (connectionAttemptActive) {
    if (millis() - connectionAttemptAt < 20000UL) {
      return;
    }
    connectionAttemptActive = false;
    if (rememberPendingNetwork) {
      lastNetworkError = "No se pudo conectar a " + pendingNetwork.ssid;
      recordError("Wi-Fi", "No se pudo conectar a la red STA configurada");
    }
    rememberPendingNetwork = false;
    connectionMessage = "STA desconectada";
    nextReconnectAt = millis() + 30000UL;
  }

  if (savedNetworks.empty() || millis() < nextReconnectAt) {
    return;
  }
  if (WiFi.softAPgetStationNum() > 0) {
    // Con un cliente en el portal no se escanea ni se reintenta: se espera a
    // que cargue manualmente la nueva red.
    nextReconnectAt = millis() + 5000UL;
    return;
  }

  // Solo se intenta conectar si una red guardada está realmente visible.
  const bool scanned = scanNearbyNetworks();
  const SavedNetwork *chosen = nullptr;
  int bestRssi = -1000;
  if (scanned) {
    for (const ScannedNetwork &seen : scanResults) {
      for (const SavedNetwork &saved : savedNetworks) {
        if (saved.ssid == seen.ssid && seen.rssi > bestRssi) {
          chosen = &saved;
          bestRssi = seen.rssi;
        }
      }
    }
  }
  if (chosen == nullptr) {
    nextReconnectAt = millis() + 120000UL;
    connectionMessage = "Sin red de confianza visible; carga una red desde el portal";
    return;
  }
  nextReconnectAt = millis() + 25000UL;
  startStaConnection(*chosen, false);
  Serial.printf("Reconexión automática a la red guardada: %s\n",
                chosen->ssid.c_str());
}

bool scanNearbyNetworks() {
  scanResults.clear();
  if (WiFi.status() != WL_CONNECTED) {
    // Un intento de reconexión en curso impide escanear; se cancela y se pausa.
    WiFi.disconnect(false, false);
    connectionAttemptActive = false;
    rememberPendingNetwork = false;
    delay(100);
  }
  Serial.println(F("Iniciando escaneo pasivo Wi-Fi (2.4 GHz)"));
  const int count = WiFi.scanNetworks(false, false, true, 300);
  nextReconnectAt = millis() + 45000UL;
  if (count < 0) {
    Serial.printf("ERROR: escaneo Wi-Fi falló (%d)\n", count);
    return false;
  }

  scanResults.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    ScannedNetwork network;
    network.ssid = WiFi.SSID(i);
    network.bssid = WiFi.BSSIDstr(i);
    network.rssi = WiFi.RSSI(i);
    network.channel = static_cast<uint8_t>(WiFi.channel(i));
    network.auth = WiFi.encryptionType(i);
    scanResults.push_back(network);
  }
  WiFi.scanDelete();
  Serial.printf("Escaneo pasivo completado: %u redes\n",
                static_cast<unsigned>(scanResults.size()));
  return true;
}

float channelCrowdingScore(uint8_t candidate) {
  float score = 0;
  for (const ScannedNetwork &network : scanResults) {
    if (network.ssid == AP_SSID) {
      continue;
    }
    const int distance = abs(static_cast<int>(candidate) - network.channel);
    if (distance > 4) {
      continue;
    }
    const float signalWeight =
        std::max(0.0f, std::min(1.0f, (network.rssi + 100.0f) / 70.0f));
    score += (5 - distance) * signalWeight;
  }
  return score;
}

String channelRecommendation() {
  if (scanResults.empty()) {
    return F("Escanea redes para comparar los canales 1, 6 y 11.");
  }

  constexpr uint8_t candidates[] = {1, 6, 11};
  uint8_t bestChannel = candidates[0];
  float bestScore = channelCrowdingScore(bestChannel);
  for (size_t i = 1; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
    const float score = channelCrowdingScore(candidates[i]);
    if (score < bestScore) {
      bestScore = score;
      bestChannel = candidates[i];
    }
  }

  String advice = "Canal sugerido: ";
  advice += String(bestChannel);
  advice += " · solapamiento relativo ";
  advice += String(bestScore, 1);
  advice += ". Cámbialo manualmente en el router y vuelve a medir.";
  return advice;
}

String statusJson() {
  const bool staConnected = WiFi.status() == WL_CONNECTED;
  const String currentGatewayIp =
      staConnected ? WiFi.gatewayIP().toString() : String();
  const String currentGatewayNetwork =
      staConnected ? WiFi.SSID() + "|" + WiFi.BSSIDstr() : String();
  const String currentGatewayMac =
      staConnected && gatewayMacIp == currentGatewayIp &&
              gatewayMacNetwork == currentGatewayNetwork
          ? gatewayMac
          : String();
  String currentGatewayManufacturer;
  String currentGatewayManufacturerStatus =
      currentGatewayMac.isEmpty()
          ? "Esperando MAC del gateway"
          : gatewayVendorMutex == nullptr
                ? "No se pudo inicializar la consulta"
                : "No se pudo leer el estado de la consulta";
  if (!currentGatewayMac.isEmpty() && gatewayVendorMutex != nullptr &&
      xSemaphoreTake(gatewayVendorMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    const String lookupKey =
        currentGatewayNetwork + "|" + currentGatewayIp + "|" + currentGatewayMac;
    if (gatewayVendorLookupKey == lookupKey) {
      currentGatewayManufacturer = gatewayManufacturer;
      currentGatewayManufacturerStatus = gatewayManufacturerStatus;
    }
    xSemaphoreGive(gatewayVendorMutex);
  }
  const String currentGatewayLookupUrl =
      currentGatewayMac.isEmpty()
          ? String()
          : String(GATEWAY_VENDOR_URL) + currentGatewayMac;
  String body;
  body.reserve(512 + savedNetworks.size() * 40);
  body = F("{\"device\":");
  body += deviceInfoJson();
  body += F(",\"apSsid\":\"");
  body += AP_SSID;
  body += F("\",\"apIp\":\"");
  body += WiFi.softAPIP().toString();
  body += F("\",\"apClients\":");
  body += String(WiFi.softAPgetStationNum());
  body += F(",\"staConnected\":");
  body += WiFi.status() == WL_CONNECTED ? F("true") : F("false");
  body += F(",\"staSsid\":\"");
  if (WiFi.status() == WL_CONNECTED) {
    body += jsonEscape(WiFi.SSID());
  }
  body += F("\",\"staIp\":\"");
  if (WiFi.status() == WL_CONNECTED) {
    body += WiFi.localIP().toString();
  }
  body += F("\",\"staGateway\":\"");
  if (WiFi.status() == WL_CONNECTED) {
    body += WiFi.gatewayIP().toString();
  }
  body += F("\",\"staGatewayMac\":\"");
  body += jsonEscape(currentGatewayMac);
  body += F("\",\"staGatewayManufacturer\":\"");
  body += jsonEscape(currentGatewayManufacturer);
  body += F("\",\"staGatewayManufacturerStatus\":\"");
  body += jsonEscape(currentGatewayManufacturerStatus);
  body += F("\",\"staGatewayLookupUrl\":\"");
  body += jsonEscape(currentGatewayLookupUrl);
  body += F("\",\"rssi\":");
  body += WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : F("null");
  body += F(",\"channel\":");
  body += WiFi.status() == WL_CONNECTED ? String(WiFi.channel()) : F("null");
  body += F(",\"bleProvisioningActive\":");
  body += bleProvisioningActive ? F("true") : F("false");
  body += F(",\"bleServiceName\":\"");
  body += jsonEscape(bleServiceName);
  body += F("\",\"blePop\":\"");
  body += bleProvisioningActive ? bleProofOfPossession : "";
  body += '"';
  body += F(",\"message\":\"");
  body += jsonEscape(connectionMessage);
  body += F("\",\"error\":\"");
  body += jsonEscape(lastNetworkError);
  body += F("\",\"channelAdvice\":\"");
  body += jsonEscape(channelRecommendation());
  body += F("\",\"filesystemReady\":");
  body += filesystemReady ? F("true") : F("false");
  body += F(",\"sdReady\":");
  body += sdCardReady ? F("true") : F("false");
  body += F(",\"sdMessage\":\"");
  body += jsonEscape(sdFormatRunning ? String("Formateando microSD")
                                     : sdLastMessage);
  body += '"';
  body += F(",\"sdFormatRunning\":");
  body += sdFormatRunning ? F("true") : F("false");
  body += F(",\"sdFormatAttempted\":");
  body += sdFormatAttempted ? F("true") : F("false");
  body += F(",\"sdFormatSucceeded\":");
  body += sdFormatSucceeded ? F("true") : F("false");
  body += F(",\"filesystemTotalMb\":");
  body += filesystemReady
              ? String(static_cast<double>(LittleFS.totalBytes()) / 1048576.0,
                       1)
              : F("0");
  body += F(",\"filesystemUsedMb\":");
  body += filesystemReady
              ? String(static_cast<double>(LittleFS.usedBytes()) / 1048576.0,
                       1)
              : F("0");
  body += F(",\"sdTotalMb\":");
  body += sdCardReady && !sdFormatRunning
              ? String(static_cast<double>(SD_MMC.totalBytes()) / 1048576.0, 1)
              : F("0");
  body += F(",\"sdUsedMb\":");
  body += sdCardReady && !sdFormatRunning
              ? String(static_cast<double>(SD_MMC.usedBytes()) / 1048576.0, 1)
              : F("0");
  body += F(",\"filesystemTotalBytes\":");
  body += filesystemReady
              ? String(static_cast<unsigned long long>(LittleFS.totalBytes()))
              : F("0");
  body += F(",\"filesystemUsedBytes\":");
  body += filesystemReady
              ? String(static_cast<unsigned long long>(LittleFS.usedBytes()))
              : F("0");
  body += F(",\"sdTotalBytes\":");
  body += sdCardReady && !sdFormatRunning
              ? String(static_cast<unsigned long long>(SD_MMC.totalBytes()))
              : F("0");
  body += F(",\"sdUsedBytes\":");
  body += sdCardReady && !sdFormatRunning
              ? String(static_cast<unsigned long long>(SD_MMC.usedBytes()))
              : F("0");
  body += F(",\"whitelist\":[");
  for (size_t i = 0; i < savedNetworks.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    body += '"';
    body += jsonEscape(savedNetworks[i].ssid);
    body += '"';
  }
  body += F("]}");
  return body;
}

bool parseFloatArgument(const String &input, float &value) {
  if (input.isEmpty()) {
    return false;
  }
  char *end = nullptr;
  const float parsed = strtof(input.c_str(), &end);
  if (end == input.c_str() || *end != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  value = parsed;
  return true;
}

PointAnalysis analyzePoint(const SurveyPoint &point,
                           const SurveyPoint *previous) {
  const auto percent = [](float value) {
    return std::max(0.0f, std::min(100.0f, value));
  };
  const bool hasRssi = point.rssi < 0;
  const float signalScore =
      hasRssi ? percent((point.rssi + 90.0f) * (100.0f / 35.0f)) : 50.0f;
  const float throughputScore = percent(point.mbps * 20.0f);
  const float latencyScore = percent(100.0f - (point.latencyMs - 20.0f) *
                                                   (100.0f / 130.0f));
  PointAnalysis analysis;
  analysis.score = static_cast<uint8_t>(
      lroundf(signalScore * 0.45f + throughputScore * 0.35f +
              latencyScore * 0.20f));

  if (previous != nullptr) {
    if (previous->mbps >= 1.0f && point.mbps < previous->mbps * 0.70f) {
      analysis.anomaly = F("velocidad cayó más del 30%");
    }
    if (hasRssi && previous->rssi < 0 && point.rssi <= previous->rssi - 10) {
      if (!analysis.anomaly.isEmpty()) {
        analysis.anomaly += F("; ");
      }
      analysis.anomaly += F("RSSI cayó 10 dB o más");
    }
    if (point.latencyMs > previous->latencyMs * 2.0f &&
        point.latencyMs > previous->latencyMs + 20.0f) {
      if (!analysis.anomaly.isEmpty()) {
        analysis.anomaly += F("; ");
      }
      analysis.anomaly += F("latencia aumentó más del doble");
    }
  }

  if (!analysis.anomaly.isEmpty()) {
    analysis.quality = F("Inestable");
    analysis.recommendation =
        "Cambio detectado frente a la medición anterior: " + analysis.anomaly +
        ". Repite la prueba y revisa obstáculos o carga de red.";
  } else if (analysis.score >= 80) {
    analysis.quality = F("Buena");
    analysis.recommendation =
        F("Buen resultado inicial; valida con el flujo real de la cámara.");
  } else if (analysis.score >= 60) {
    analysis.quality = F("Aceptable");
    analysis.recommendation =
        F("Hay margen de mejora; compara otro punto o canal.");
  } else {
    analysis.quality = F("Débil");
    if (hasRssi && point.rssi < -75) {
      analysis.recommendation =
          F("Señal baja: acerca el punto al router o reduce obstáculos.");
    } else if (point.mbps < 5.0f) {
      analysis.recommendation =
          F("Rendimiento bajo frente a la referencia de 5 Mbps; repite la prueba.");
    } else {
      analysis.recommendation =
          F("Latencia alta: revisa congestión y vuelve a medir.");
    }
  }
  return analysis;
}

bool parsePointCsvLine(String line, SurveyPoint &point) {
  line.trim();
  if (line.isEmpty() || line.startsWith("uptime_ms")) {
    return false;
  }

  String fields[6];
  size_t fieldIndex = 0;
  int start = 0;
  for (size_t i = 0; i <= line.length(); ++i) {
    if (i == line.length() || line[i] == ',') {
      if (fieldIndex == 6) {
        return false;
      }
      fields[fieldIndex++] = line.substring(start, i);
      start = static_cast<int>(i + 1);
    }
  }
  if (fieldIndex != 6 || fields[0].isEmpty() || fields[1].isEmpty() ||
      fields[3].isEmpty() || fields[4].isEmpty()) {
    return false;
  }

  char *end = nullptr;
  const unsigned long long uptime = strtoull(fields[0].c_str(), &end, 10);
  if (end == fields[0].c_str() || *end != '\0' || fields[0][0] == '-' ||
      uptime > UINT32_MAX) {
    return false;
  }
  point.uptimeMs = static_cast<uint32_t>(uptime);
  point.name = fields[1];
  float mbps = 0;
  float latencyMs = 0;
  if (!parseFloatArgument(fields[2], mbps) ||
      !parseFloatArgument(fields[5], latencyMs) || mbps < 0 ||
      mbps > 10000 || latencyMs < 0 || latencyMs > 120000) {
    return false;
  }

  end = nullptr;
  const long rssi = strtol(fields[3].c_str(), &end, 10);
  if (end == fields[3].c_str() || *end != '\0' || rssi < -127 || rssi > 0) {
    return false;
  }
  end = nullptr;
  const long channel = strtol(fields[4].c_str(), &end, 10);
  if (end == fields[4].c_str() || *end != '\0' || channel < 0 ||
      channel > UINT8_MAX) {
    return false;
  }

  point.mbps = mbps;
  point.rssi = static_cast<int32_t>(rssi);
  point.channel = static_cast<uint8_t>(channel);
  point.latencyMs = latencyMs;
  return true;
}

bool findPreviousPoint(const String &name, SurveyPoint &previous) {
  if (!filesystemReady || !LittleFS.exists(POINTS_FILE)) {
    return false;
  }
  File file = LittleFS.open(POINTS_FILE, FILE_READ);
  if (!file) {
    Serial.println(F("ERROR: no se pudo leer el historial para detectar cambios"));
    return false;
  }
  bool found = false;
  while (file.available()) {
    SurveyPoint candidate;
    if (parsePointCsvLine(file.readStringUntil('\n'), candidate) &&
        candidate.name == name) {
      previous = candidate;
      found = true;
    }
  }
  file.close();
  return found;
}

String sanitizePointName(String name) {
  name.trim();
  String sanitized;
  sanitized.reserve(name.length());
  for (size_t i = 0; i < name.length() && sanitized.length() < 32; ++i) {
    const char c = name[i];
    const bool valid =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ';
    sanitized += valid ? c : '_';
  }
  sanitized.trim();
  return sanitized;
}

bool appendSurveyPoint(const SurveyPoint &point) {
  if (!filesystemReady) {
    return false;
  }

  const bool needsHeader = !LittleFS.exists(POINTS_FILE);
  File file = LittleFS.open(POINTS_FILE, FILE_APPEND);
  if (!file) {
    Serial.println(F("ERROR: no se pudo abrir points.csv para escritura"));
    return false;
  }
  if (needsHeader) {
    file.println(F("uptime_ms,name,mbps,rssi_dbm,channel,latency_ms"));
  }
  char line[128];
  const int lineLength =
      snprintf(line, sizeof(line), "%lu,%s,%.3f,%ld,%u,%.2f\n",
               static_cast<unsigned long>(point.uptimeMs), point.name.c_str(),
               point.mbps, static_cast<long>(point.rssi), point.channel,
               point.latencyMs);
  if (lineLength < 0 || static_cast<size_t>(lineLength) >= sizeof(line)) {
    file.close();
    Serial.println(F("ERROR: la fila CSV excede el tamaño permitido"));
    return false;
  }
  const size_t written =
      file.write(reinterpret_cast<const uint8_t *>(line),
                 static_cast<size_t>(lineLength));
  file.close();
  if (written != static_cast<size_t>(lineLength)) {
    Serial.println(F("ERROR: no se pudo escribir el punto de medición"));
    return false;
  }
  return true;
}

bool pointsJson(String &body) {
  body = '[';
  if (!filesystemReady) {
    return false;
  }
  if (!LittleFS.exists(POINTS_FILE)) {
    body += ']';
    return true;
  }

  File file = LittleFS.open(POINTS_FILE, FILE_READ);
  if (!file) {
    Serial.println(F("ERROR: no se pudo abrir points.csv para lectura"));
    return false;
  }

  const size_t csvBytes = file.size();
  const size_t estimatedJsonBytes = csvBytes * 8 + 2;
  if (!body.reserve(estimatedJsonBytes)) {
    Serial.printf("ERROR: no hay memoria para preparar JSON de puntos (%u bytes)\n",
                  static_cast<unsigned>(estimatedJsonBytes));
    file.close();
    return false;
  }

  std::vector<SurveyPoint> latestByName;
  bool first = true;
  while (file.available()) {
    SurveyPoint point;
    if (!parsePointCsvLine(file.readStringUntil('\n'), point)) {
      continue;
    }

    const SurveyPoint *previous = nullptr;
    size_t previousIndex = latestByName.size();
    for (size_t i = 0; i < latestByName.size(); ++i) {
      if (latestByName[i].name == point.name) {
        previous = &latestByName[i];
        previousIndex = i;
        break;
      }
    }
    const PointAnalysis analysis = analyzePoint(point, previous);

    if (!first) {
      body += ',';
    }
    first = false;
    body += F("{\"uptimeMs\":");
    body += String(point.uptimeMs);
    body += F(",\"name\":\"");
    body += jsonEscape(point.name);
    body += F("\",\"mbps\":");
    body += String(point.mbps, 3);
    body += F(",\"rssi\":");
    body += String(point.rssi);
    body += F(",\"channel\":");
    body += String(point.channel);
    body += F(",\"latencyMs\":");
    body += String(point.latencyMs, 2);
    body += F(",\"score\":");
    body += String(analysis.score);
    body += F(",\"quality\":\"");
    body += jsonEscape(analysis.quality);
    body += F("\",\"recommendation\":\"");
    body += jsonEscape(analysis.recommendation);
    body += F("\",\"anomaly\":\"");
    body += jsonEscape(analysis.anomaly.isEmpty() ? String("Sin anomalía")
                                                  : analysis.anomaly);
    body += '"';
    body += '}';

    if (previousIndex < latestByName.size()) {
      latestByName[previousIndex] = point;
    } else {
      latestByName.push_back(point);
    }
    latestPointName = point.name;
    latestAnalysis = analysis;
  }
  file.close();
  body += ']';
  Serial.printf("[Portal] Puntos JSON: %u bytes; CSV: %u bytes\n",
                static_cast<unsigned>(body.length()),
                static_cast<unsigned>(csvBytes));
  return true;
}

String sanitizeAssetValue(String value, size_t maxLength) {
  value.trim();
  String clean;
  clean.reserve(std::min(value.length(), maxLength));
  for (size_t i = 0; i < value.length() && clean.length() < maxLength; ++i) {
    const char c = value[i];
    const bool valid =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' ||
        c == '.' || c == '/' || c == '+' || c == '(' || c == ')' ||
        c == ':';
    clean += valid ? c : '_';
  }
  clean.trim();
  return clean;
}

bool normalizeMacAddress(String input, String &normalized) {
  input.trim();
  String hex;
  hex.reserve(12);
  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    if (c == ':' || c == '-') {
      continue;
    }
    const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                       (c >= 'A' && c <= 'F');
    if (!isHex || hex.length() == 12) {
      return false;
    }
    hex += static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  if (hex.length() != 12) {
    return false;
  }
  normalized = "";
  normalized.reserve(17);
  for (size_t i = 0; i < hex.length(); ++i) {
    if (i != 0 && i % 2 == 0) {
      normalized += ':';
    }
    normalized += hex[i];
  }
  return true;
}

bool isCctvAssetType(const String &type) {
  return type == "Camara" || type == "NVR" || type == "DVR" ||
         type == "Otro";
}

bool isCctvIpMode(const String &ipMode) {
  return ipMode == "detected" || ipMode == "manual" || ipMode == "none";
}

bool parseCctvAssetCsvLine(String line, CctvAsset &asset) {
  line.trim();
  if (line.isEmpty() || line.startsWith("ip,")) {
    return false;
  }

  String fields[8];
  size_t fieldIndex = 0;
  int start = 0;
  for (size_t i = 0; i <= line.length(); ++i) {
    if (i == line.length() || line[i] == ',') {
      if (fieldIndex == 8) {
        return false;
      }
      fields[fieldIndex++] = line.substring(start, i);
      start = static_cast<int>(i + 1);
    }
  }
  if ((fieldIndex != 7 && fieldIndex != 8) || fields[1].isEmpty() ||
      fields[2].isEmpty()) {
    return false;
  }

  IPAddress parsedIp;
  String normalizedMac;
  const String ipMode = fieldIndex == 8 ? fields[7] : String("detected");
  if (!isCctvIpMode(ipMode) || !isCctvAssetType(fields[2]) ||
      (!fields[3].isEmpty() && !normalizeMacAddress(fields[3], normalizedMac))) {
    return false;
  }
  if (ipMode == "none") {
    if (!fields[0].isEmpty()) {
      return false;
    }
    asset.ip = "";
  } else if (!parsedIp.fromString(fields[0])) {
    return false;
  } else {
    asset.ip = parsedIp.toString();
  }

  asset.name = fields[1];
  asset.type = fields[2];
  asset.mac = normalizedMac;
  asset.manufacturer = fields[4];
  asset.model = fields[5];
  asset.firmware = fields[6];
  asset.ipMode = ipMode;
  return true;
}

bool loadCctvAssets() {
  cctvAssets.clear();
  if (!filesystemReady || !LittleFS.exists(CCTV_ASSETS_FILE)) {
    return filesystemReady;
  }

  File file = LittleFS.open(CCTV_ASSETS_FILE, FILE_READ);
  if (!file) {
    Serial.println(F("ERROR: no se pudo abrir el inventario CCTV"));
    return false;
  }
  while (file.available()) {
    CctvAsset asset;
    if (parseCctvAssetCsvLine(file.readStringUntil('\n'), asset)) {
      cctvAssets.push_back(asset);
    }
  }
  file.close();
  Serial.printf("Inventario CCTV cargado: %u equipos\n",
                static_cast<unsigned>(cctvAssets.size()));
  return true;
}

String cctvAssetsJson() {
  String body;
  body.reserve(128 + cctvAssets.size() * 180);
  body = '[';
  for (size_t i = 0; i < cctvAssets.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    const CctvAsset &asset = cctvAssets[i];
    body += F("{\"ip\":\"");
    body += jsonEscape(asset.ip);
    body += F("\",\"name\":\"");
    body += jsonEscape(asset.name);
    body += F("\",\"type\":\"");
    body += jsonEscape(asset.type);
    body += F("\",\"mac\":\"");
    body += jsonEscape(asset.mac);
    body += F("\",\"manufacturer\":\"");
    body += jsonEscape(asset.manufacturer);
    body += F("\",\"model\":\"");
    body += jsonEscape(asset.model);
    body += F("\",\"firmware\":\"");
    body += jsonEscape(asset.firmware);
    body += F("\",\"ipMode\":\"");
    body += jsonEscape(asset.ipMode);
    body += F("\",\"networkName\":\"");
    body += jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String(""));
    body += F("\"}");
  }
  body += ']';
  return body;
}

String arpNeighborsJson() {
  String body;
  body.reserve(128 + arpObservations.size() * 120);
  body = '[';
  for (size_t i = 0; i < arpObservations.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    const ArpObservation &entry = arpObservations[i];
    body += F("{\"ip\":\"");
    body += jsonEscape(entry.ip);
    body += F("\",\"mac\":\"");
    body += jsonEscape(entry.mac);
    body += F("\",\"lastSeenSeconds\":");
    body += String((millis() - entry.lastSeenAt) / 1000UL);
    body += '}';
  }
  body += ']';
  return body;
}

bool appendCctvAsset(const CctvAsset &asset) {
  const bool needsHeader = !LittleFS.exists(CCTV_ASSETS_FILE);
  File file = LittleFS.open(CCTV_ASSETS_FILE, FILE_APPEND);
  if (!file) {
    Serial.println(F("ERROR: no se pudo abrir el inventario CCTV para escritura"));
    return false;
  }
  if (needsHeader) {
    file.println(F("ip,name,type,mac,manufacturer,model,firmware,ip_mode"));
  }
  char line[288];
  const int lineLength =
      snprintf(line, sizeof(line), "%s,%s,%s,%s,%s,%s,%s,%s\n",
               asset.ip.c_str(), asset.name.c_str(), asset.type.c_str(),
               asset.mac.c_str(), asset.manufacturer.c_str(),
               asset.model.c_str(), asset.firmware.c_str(),
               asset.ipMode.c_str());
  if (lineLength < 0 || static_cast<size_t>(lineLength) >= sizeof(line)) {
    file.close();
    Serial.println(F("ERROR: el registro CCTV excede el tamaño permitido"));
    return false;
  }
  const size_t written =
      file.write(reinterpret_cast<const uint8_t *>(line),
                 static_cast<size_t>(lineLength));
  file.close();
  if (written != static_cast<size_t>(lineLength)) {
    Serial.println(F("ERROR: no se pudo escribir el registro CCTV completo"));
    return false;
  }
  return true;
}

bool isValidCctvHistoryDate(const String &date) {
  if (date.length() != 10 || date[4] != '-' || date[7] != '-') {
    return false;
  }
  for (size_t i = 0; i < date.length(); ++i) {
    if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) {
      return false;
    }
  }
  const int year = date.substring(0, 4).toInt();
  const int month = date.substring(5, 7).toInt();
  const int day = date.substring(8, 10).toInt();
  if (year < 1970 || year > 2099 || month < 1 || month > 12) {
    return false;
  }
  const bool leapYear =
      year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const int daysInMonth[] = {31, leapYear ? 29 : 28, 31, 30, 31, 30,
                             31, 31, 30, 31, 30, 31};
  return day >= 1 && day <= daysInMonth[month - 1];
}

bool isValidCctvHistoryTime(const String &time) {
  if (time.length() != 8 || time[2] != ':' || time[5] != ':') {
    return false;
  }
  for (size_t i = 0; i < time.length(); ++i) {
    if (i != 2 && i != 5 && (time[i] < '0' || time[i] > '9')) {
      return false;
    }
  }
  const int hour = time.substring(0, 2).toInt();
  const int minute = time.substring(3, 5).toInt();
  const int second = time.substring(6, 8).toInt();
  return hour <= 23 && minute <= 59 && second <= 59;
}

String discoveryHistoryCsvLine(const DiscoveryHistoryRecord &record) {
  String line;
  line.reserve(320);
  line += csvEscape(record.date);
  line += ',';
  line += csvEscape(record.time);
  line += ',';
  line += csvEscape(record.source);
  line += ',';
  line += csvEscape(record.ssid);
  line += ',';
  line += csvEscape(record.gatewayIp);
  line += ',';
  line += csvEscape(record.ip);
  line += ',';
  line += csvEscape(record.protocol);
  line += ',';
  line += csvEscape(record.identity);
  line += ',';
  line += csvEscape(record.endpoint);
  line += ',';
  line += csvEscape(record.mac);
  line += ',';
  line += csvEscape(record.model);
  line += ',';
  line += csvEscape(record.deviceClass);
  line += ',';
  line += csvEscape(record.firmware);
  line += ',';
  line += csvEscape(record.serial);
  line += F("\r\n");
  return line;
}

bool parseDiscoveryHistoryCsvLine(String line,
                                  DiscoveryHistoryRecord &record) {
  line.trim();
  if (line.isEmpty() || line.startsWith("date,")) {
    return false;
  }

  String fields[14];
  size_t fieldIndex = 0;
  String field;
  bool quoted = false;
  bool quoteClosed = false;
  for (size_t i = 0; i < line.length(); ++i) {
    const char character = line[i];
    if (quoted) {
      if (character == '"') {
        if (i + 1 < line.length() && line[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          quoted = false;
          quoteClosed = true;
        }
      } else {
        field += character;
      }
      continue;
    }

    if (character == ',') {
      if (fieldIndex >= 14) {
        return false;
      }
      fields[fieldIndex++] = field;
      field = "";
      quoteClosed = false;
    } else if (character == '"' && field.isEmpty() && !quoteClosed) {
      quoted = true;
    } else if (quoteClosed || character == '"') {
      return false;
    } else {
      field += character;
    }
  }
  if (quoted || fieldIndex >= 14) {
    return false;
  }
  fields[fieldIndex++] = field;
  if (fieldIndex != 14 || !isValidCctvHistoryDate(fields[0]) ||
      !isValidCctvHistoryTime(fields[1]) || fields[2].isEmpty() ||
      fields[5].isEmpty() || fields[6].isEmpty()) {
    return false;
  }

  IPAddress parsedIp;
  if (!parsedIp.fromString(fields[5])) {
    return false;
  }
  if (fields[2] != "SADP" && fields[2] != "DHDiscover" &&
      fields[2] != "GENERIC") {
    return false;
  }

  record.date = fields[0];
  record.time = fields[1];
  record.source = fields[2];
  record.ssid = fields[3];
  record.gatewayIp = fields[4];
  record.ip = parsedIp.toString();
  record.protocol = fields[6];
  record.identity = fields[7];
  record.endpoint = fields[8];
  record.mac = fields[9];
  record.model = fields[10];
  record.deviceClass = fields[11];
  record.firmware = fields[12];
  record.serial = fields[13];
  return true;
}

bool ensureDiscoveryHistoryFile() {
  if (!sdCardReady || sdFormatRunning) {
    return false;
  }
  if (SD_MMC.exists(DISCOVERY_HISTORY_FILE)) {
    File existing = SD_MMC.open(DISCOVERY_HISTORY_FILE, FILE_READ);
    if (!existing) {
      return false;
    }
    const size_t existingSize = existing.size();
    const String header = existing.readStringUntil('\n');
    existing.close();
    if (existingSize > 0) {
      return header.startsWith("date,time,source,");
    }
  }

  File file = SD_MMC.open(DISCOVERY_HISTORY_FILE, FILE_APPEND);
  if (!file) {
    return false;
  }
  const String header =
      F("date,time,source,ssid,gateway_ip,ip,protocol,identity,endpoint,mac,"
        "model,device_class,firmware,serial\r\n");
  const bool written =
      file.write(reinterpret_cast<const uint8_t *>(header.c_str()),
                 header.length()) == header.length();
  file.flush();
  file.close();
  return written;
}

DiscoveryHistorySaveStatus saveDiscoveryHistory(
    const std::vector<DiscoveryHistoryRecord> &records, const String &date,
    const String &time) {
  DiscoveryHistorySaveStatus status{true, 0, ""};
  if (records.empty()) {
    return status;
  }
  if (!isValidCctvHistoryDate(date) || !isValidCctvHistoryTime(time)) {
    status.saved = false;
    status.error = F("La fecha u hora local recibida no es válida");
    return status;
  }
  if (!sdCardReady || sdFormatRunning) {
    status.saved = false;
    status.error = F("La microSD no está disponible o se está formateando");
    return status;
  }
  if (!ensureDiscoveryHistoryFile()) {
    status.saved = false;
    status.error = F("No se pudo preparar el archivo de historial en la microSD");
    Serial.println(F("ERROR: no se pudo preparar el historial de descubrimientos"));
    return status;
  }

  File file = SD_MMC.open(DISCOVERY_HISTORY_FILE, FILE_APPEND);
  if (!file) {
    status.saved = false;
    status.error = F("No se pudo abrir el historial de descubrimientos en la microSD");
    Serial.println(F("ERROR: no se pudo abrir el historial de descubrimientos"));
    return status;
  }
  const String ssid = WiFi.SSID();
  const String gatewayIp = WiFi.gatewayIP().toString();
  for (const DiscoveryHistoryRecord &sourceRecord : records) {
    DiscoveryHistoryRecord record = sourceRecord;
    record.date = date;
    record.time = time;
    record.ssid = ssid;
    record.gatewayIp = gatewayIp;
    const String line = discoveryHistoryCsvLine(record);
    if (file.write(reinterpret_cast<const uint8_t *>(line.c_str()),
                   line.length()) != line.length()) {
      status.saved = false;
      status.error = "La microSD no aceptó todos los registros (" +
                     String(status.savedCount) + " de " +
                     String(records.size()) + ")";
      Serial.println(F("ERROR: escritura parcial del historial de descubrimientos"));
      break;
    }
    ++status.savedCount;
  }
  file.flush();
  file.close();
  if (status.savedCount != records.size()) {
    status.saved = false;
  }
  return status;
}

bool loadDiscoveryHistory(std::vector<DiscoveryHistoryRecord> &records,
                          bool &truncated) {
  records.clear();
  truncated = false;
  if (!ensureDiscoveryHistoryFile()) {
    return false;
  }
  File file = SD_MMC.open(DISCOVERY_HISTORY_FILE, FILE_READ);
  if (!file) {
    return false;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line.startsWith("date,")) {
      continue;
    }
    DiscoveryHistoryRecord record;
    if (!parseDiscoveryHistoryCsvLine(line, record)) {
      file.close();
      Serial.println(F("ERROR: fila invalida en historial de descubrimientos"));
      return false;
    }
    if (records.size() == MAX_DISCOVERY_HISTORY_ROWS) {
      records.erase(records.begin());
      truncated = true;
    }
    records.push_back(record);
  }
  file.close();
  std::reverse(records.begin(), records.end());
  return true;
}

void appendDiscoveryHistorySaveStatus(
    String &body, const DiscoveryHistorySaveStatus &status) {
  body += F(",\"saved\":");
  body += status.saved ? F("true") : F("false");
  body += F(",\"savedCount\":");
  body += String(status.savedCount);
  body += F(",\"saveError\":\"");
  body += jsonEscape(status.error);
  body += '"';
}

String discoveryHistoryJson(
    const std::vector<DiscoveryHistoryRecord> &records, bool truncated) {
  String body;
  body.reserve(128 + records.size() * 300);
  body = F("{\"truncated\":");
  body += truncated ? F("true") : F("false");
  body += F(",\"records\":[");
  for (size_t i = 0; i < records.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    const DiscoveryHistoryRecord &record = records[i];
    body += F("{\"date\":\"");
    body += jsonEscape(record.date);
    body += F("\",\"time\":\"");
    body += jsonEscape(record.time);
    body += F("\",\"source\":\"");
    body += jsonEscape(record.source);
    body += F("\",\"ssid\":\"");
    body += jsonEscape(record.ssid);
    body += F("\",\"gatewayIp\":\"");
    body += jsonEscape(record.gatewayIp);
    body += F("\",\"ip\":\"");
    body += jsonEscape(record.ip);
    body += F("\",\"protocol\":\"");
    body += jsonEscape(record.protocol);
    body += F("\",\"identity\":\"");
    body += jsonEscape(record.identity);
    body += F("\",\"endpoint\":\"");
    body += jsonEscape(record.endpoint);
    body += F("\",\"mac\":\"");
    body += jsonEscape(record.mac);
    body += F("\",\"model\":\"");
    body += jsonEscape(record.model);
    body += F("\",\"deviceClass\":\"");
    body += jsonEscape(record.deviceClass);
    body += F("\",\"firmware\":\"");
    body += jsonEscape(record.firmware);
    body += F("\",\"serial\":\"");
    body += jsonEscape(record.serial);
    body += F("\"}");
  }
  body += F("]}");
  return body;
}

String cctvHistoryCsvLine(const CctvHistoryRecord &record) {
  String line;
  line.reserve(256);
  line += csvEscape(record.date);
  line += ',';
  line += csvEscape(record.time);
  line += ',';
  line += csvEscape(record.asset.ip);
  line += ',';
  line += csvEscape(record.asset.name);
  line += ',';
  line += csvEscape(record.asset.type);
  line += ',';
  line += csvEscape(record.asset.mac);
  line += ',';
  line += csvEscape(record.asset.manufacturer);
  line += ',';
  line += csvEscape(record.asset.model);
  line += ',';
  line += csvEscape(record.asset.firmware);
  line += ',';
  line += csvEscape(record.asset.ipMode);
  line += ',';
  if (record.hasRssi) {
    line += String(record.rssi);
  }
  line += F("\r\n");
  return line;
}

bool parseCctvHistoryCsvLine(String line, CctvHistoryRecord &record) {
  line.trim();
  if (line.isEmpty() || line.startsWith("date,")) {
    return false;
  }

  String fields[11];
  size_t fieldIndex = 0;
  int start = 0;
  for (size_t i = 0; i <= line.length(); ++i) {
    if (i == line.length() || line[i] == ',') {
      if (fieldIndex == 11) {
        return false;
      }
      fields[fieldIndex++] = line.substring(start, i);
      start = static_cast<int>(i + 1);
    }
  }
  if (fieldIndex != 11 ||
      ((!fields[0].isEmpty() || !fields[1].isEmpty()) &&
       (!isValidCctvHistoryDate(fields[0]) ||
        !isValidCctvHistoryTime(fields[1]))) ||
      fields[3].isEmpty() || !isCctvAssetType(fields[4]) ||
      !isCctvIpMode(fields[9])) {
    return false;
  }

  IPAddress parsedIp;
  String normalizedMac;
  if (fields[9] == "none") {
    if (!fields[2].isEmpty()) {
      return false;
    }
  } else if (!parsedIp.fromString(fields[2])) {
    return false;
  }
  if (!fields[5].isEmpty() &&
      !normalizeMacAddress(fields[5], normalizedMac)) {
    return false;
  }

  record.date = fields[0];
  record.time = fields[1];
  record.asset.ip = fields[2].isEmpty() ? String() : parsedIp.toString();
  record.asset.name = fields[3];
  record.asset.type = fields[4];
  record.asset.mac = normalizedMac;
  record.asset.manufacturer = fields[6];
  record.asset.model = fields[7];
  record.asset.firmware = fields[8];
  record.asset.ipMode = fields[9];
  record.hasRssi = !fields[10].isEmpty();
  record.rssi = 0;
  if (record.hasRssi) {
    char *end = nullptr;
    const long rssi = strtol(fields[10].c_str(), &end, 10);
    if (end == fields[10].c_str() || *end != '\0' || rssi < -127 ||
        rssi > 0) {
      return false;
    }
    record.rssi = static_cast<int32_t>(rssi);
  }
  return true;
}

bool ensureCctvHistoryFile() {
  if (!sdCardReady || sdFormatRunning) {
    return false;
  }
  if (SD_MMC.exists(CCTV_HISTORY_FILE)) {
    File existing = SD_MMC.open(CCTV_HISTORY_FILE, FILE_READ);
    if (!existing) {
      return false;
    }
    const bool hasContent = existing.size() > 0;
    existing.close();
    if (hasContent) {
      return true;
    }
  }

  File file = SD_MMC.open(CCTV_HISTORY_FILE, FILE_APPEND);
  if (!file) {
    return false;
  }
  const String header =
      F("date,time,ip,name,type,mac,manufacturer,model,firmware,ip_mode,"
        "wifi_rssi_dbm\r\n");
  if (file.write(reinterpret_cast<const uint8_t *>(header.c_str()),
                 header.length()) != header.length()) {
    file.close();
    return false;
  }
  for (const CctvAsset &asset : cctvAssets) {
    const CctvHistoryRecord legacyRecord{"", "", asset, false, 0};
    const String line = cctvHistoryCsvLine(legacyRecord);
    if (file.write(reinterpret_cast<const uint8_t *>(line.c_str()),
                   line.length()) != line.length()) {
      file.close();
      return false;
    }
  }
  file.flush();
  file.close();
  return true;
}

bool appendCctvHistoryRecord(const CctvHistoryRecord &record) {
  if (!ensureCctvHistoryFile()) {
    Serial.println(F("ERROR: no se pudo preparar el historial CCTV en microSD"));
    return false;
  }
  File file = SD_MMC.open(CCTV_HISTORY_FILE, FILE_APPEND);
  if (!file) {
    Serial.println(F("ERROR: no se pudo abrir el historial CCTV en microSD"));
    return false;
  }
  const String line = cctvHistoryCsvLine(record);
  const size_t written =
      file.write(reinterpret_cast<const uint8_t *>(line.c_str()), line.length());
  file.flush();
  file.close();
  if (written != line.length()) {
    Serial.println(F("ERROR: no se pudo escribir el registro CCTV completo en microSD"));
    return false;
  }
  return true;
}

bool loadCctvHistory(std::vector<CctvHistoryRecord> &records,
                     size_t maxRecords = 0, const String *dateFilter = nullptr) {
  records.clear();
  if (!ensureCctvHistoryFile()) {
    return false;
  }
  File file = SD_MMC.open(CCTV_HISTORY_FILE, FILE_READ);
  if (!file) {
    return false;
  }
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.isEmpty() || line.startsWith("date,")) {
      continue;
    }
    CctvHistoryRecord record;
    if (!parseCctvHistoryCsvLine(line, record)) {
      file.close();
      Serial.println(F("ERROR: fila invalida en historial CCTV de microSD"));
      return false;
    }
    if (dateFilter != nullptr && record.date != *dateFilter) {
      continue;
    }
    if (maxRecords > 0 && records.size() >= maxRecords) {
      records.erase(records.begin());
    }
    records.push_back(record);
  }
  file.close();
  return true;
}

void updateDisplay() {
  if (!displayReady || bootQrScreen != BootQrScreen::Status) {
    return;
  }

  display->fillScreen(RGB565_BLACK);
  display->setTextWrap(false);
  display->setTextSize(1);
  display->setTextColor(RGB565_WHITE);
  display->setCursor(5, 6);
  display->println(F("ESP32 SITE SURVEY"));
  display->drawFastHLine(5, 19, 162, RGB565_CYAN);

  display->setCursor(5, 27);
  display->setTextColor(RGB565_GREEN);
  display->println(accessPointReady ? F("AP: ACTIVO") : F("AP: ERROR"));
  display->setTextColor(RGB565_WHITE);
  display->setCursor(5, 42);
  display->printf("SSID: %s", AP_SSID);
  display->setCursor(5, 57);
  display->printf("IP: %s", WiFi.softAPIP().toString().c_str());
  display->setCursor(5, 72);
  display->printf("Clientes AP: %u",
                  static_cast<unsigned>(WiFi.softAPgetStationNum()));
  display->drawFastHLine(5, 91, 162, RGB565_DARKGREY);

  display->setCursor(5, 101);
  display->setTextColor(WiFi.status() == WL_CONNECTED ? RGB565_GREEN
                                                      : RGB565_YELLOW);
  display->println(WiFi.status() == WL_CONNECTED ? F("STA: CONECTADA")
                                                 : F("STA: SIN CONEXION"));
  display->setTextColor(RGB565_WHITE);
  display->setCursor(5, 117);
  String staSsid =
      WiFi.status() == WL_CONNECTED ? WiFi.SSID() : connectionMessage;
  if (staSsid.length() > 25) {
    staSsid = staSsid.substring(0, 25);
  }
  display->printf("Red: %s", staSsid.c_str());
  display->setCursor(5, 132);
  if (WiFi.status() == WL_CONNECTED) {
    display->printf("RSSI: %d dBm", WiFi.RSSI());
  } else {
    display->println(F("RSSI: --"));
  }
  display->setCursor(5, 147);
  if (WiFi.status() == WL_CONNECTED) {
    display->printf("STA IP: %s", WiFi.localIP().toString().c_str());
  } else {
    display->println(F("STA IP: --"));
  }
  display->drawFastHLine(5, 166, 162, RGB565_DARKGREY);

  display->setCursor(5, 176);
  display->setTextColor(RGB565_CYAN);
  display->println(F("PORTAL LOCAL"));
  display->setTextColor(RGB565_WHITE);
  display->setCursor(5, 192);
  display->println(F("http://192.168.4.1"));
  display->setCursor(5, 210);
  display->printf("Redes guardadas: %u",
                  static_cast<unsigned>(savedNetworks.size()));
  display->setCursor(5, 230);
  display->printf("Puntos: %s",
                  filesystemReady && LittleFS.exists(POINTS_FILE) ? "LittleFS"
                                                                  : "0");
  display->drawFastHLine(5, 250, 162, RGB565_DARKGREY);
  display->setTextColor(RGB565_ORANGE);
  display->setCursor(5, 261);
  if (bleProvisioningActive) {
    display->printf("BLE: %s", bleServiceName);
  } else {
    display->println(F("BOOT: portal cautivo"));
  }
  display->setTextColor(RGB565_LIGHTGREY);
  display->setCursor(5, 278);
  if (bleProvisioningActive) {
    display->printf("PIN BLE: %s", bleProofOfPossession);
  } else {
    display->println(F("Solo redes autorizadas"));
  }
  display->setCursor(5, 295);
  if (latestPointName.isEmpty()) {
    display->setTextColor(RGB565_LIGHTGREY);
    display->println(F("Analisis local: sin datos"));
  } else {
    display->setTextColor(latestAnalysis.quality == "Buena" ? RGB565_GREEN
                                                            : RGB565_YELLOW);
    display->printf("Analisis: %u/100 %s",
                    static_cast<unsigned>(latestAnalysis.score),
                    latestAnalysis.quality.c_str());
    display->setCursor(5, 309);
    display->setTextColor(RGB565_WHITE);
    display->printf("Punto: %s", latestPointName.c_str());
  }
}

int provisioningQrBottom = 34;

void drawQrModules(esp_qrcode_handle_t qrCode) {
  const int qrSize = esp_qrcode_get_size(qrCode);
  constexpr int quietZone = 4;
  const int scale =
      std::max(1, std::min(3, (SCREEN_WIDTH - 12) / (qrSize + quietZone * 2)));
  const int totalSize = (qrSize + quietZone * 2) * scale;
  const int left = (SCREEN_WIDTH - totalSize) / 2;
  constexpr int top = 34;

  display->fillRect(left, top, totalSize, totalSize, RGB565_WHITE);
  for (int y = 0; y < qrSize; ++y) {
    int runStart = -1;
    for (int x = 0; x <= qrSize; ++x) {
      const bool dark = x < qrSize && esp_qrcode_get_module(qrCode, x, y);
      if (dark && runStart < 0) {
        runStart = x;
      } else if (!dark && runStart >= 0) {
        display->fillRect(left + (runStart + quietZone) * scale,
                          top + (y + quietZone) * scale,
                          (x - runStart) * scale, scale, RGB565_BLACK);
        runStart = -1;
      }
    }
  }
  provisioningQrBottom = top + totalSize;
}

void showProvisioningQrScreen() {
  display->fillScreen(RGB565_WHITE);
  display->setTextWrap(false);
  display->setTextColor(RGB565_BLACK);
  display->setTextSize(1);
  display->setCursor(8, 5);
  display->println(F("CONFIGURAR POR BLE"));
  display->setCursor(8, 19);
  display->println(F("Escanea con la app Espressif"));

  const String payload =
      String("{\"ver\":\"v1\",\"name\":\"") + bleServiceName +
      "\",\"pop\":\"" + bleProofOfPossession + "\",\"transport\":\"ble\"}";
  esp_qrcode_config_t qrConfig = ESP_QRCODE_CONFIG_DEFAULT();
  qrConfig.display_func = drawQrModules;
  qrConfig.max_qrcode_version = 6;
  const esp_err_t result = esp_qrcode_generate(&qrConfig, payload.c_str());

  display->setTextSize(1);
  display->setCursor(8, provisioningQrBottom + 6);
  if (result != ESP_OK) {
    display->println(F("No se pudo generar QR"));
  } else {
    display->printf("Equipo: %s", bleServiceName);
  }
  display->setTextSize(2);
  display->setCursor(8, provisioningQrBottom + 22);
  display->printf("PIN: %s", bleProofOfPossession);
  display->setTextSize(1);
  display->setCursor(8, provisioningQrBottom + 46);
  display->println(F("BOOT: QR de acceso STA"));
}

void showAccessPointPortalScreen() {
  display->fillScreen(RGB565_WHITE);
  display->setTextWrap(false);
  display->setTextColor(RGB565_BLACK);
  display->setTextSize(1);
  display->setCursor(8, 5);
  display->println(F("PORTAL CAUTIVO WI-FI"));
  display->setCursor(8, 19);
  display->println(F("Escanea para unirte al AP"));

  if (!accessPointReady) {
    display->setCursor(8, 52);
    display->setTextColor(RGB565_RED);
    display->println(F("Error: AP no disponible"));
    display->setTextColor(RGB565_BLACK);
    display->setCursor(8, 72);
    display->println(F("Reinicia la ESP32 y"));
    display->setCursor(8, 86);
    display->println(F("revisa el registro LOG."));
    display->setCursor(8, 112);
    display->println(F("BOOT: configurar por BLE"));
    return;
  }

  String payload = F("WIFI:T:WPA;S:");
  payload += AP_SSID;
  payload += F(";P:");
  payload += AP_PASSWORD;
  payload += F(";;");
  esp_qrcode_config_t qrConfig = ESP_QRCODE_CONFIG_DEFAULT();
  qrConfig.display_func = drawQrModules;
  qrConfig.max_qrcode_version = 6;
  const esp_err_t result = esp_qrcode_generate(&qrConfig, payload.c_str());

  display->setTextSize(1);
  display->setCursor(5, provisioningQrBottom + 5);
  if (result != ESP_OK) {
    display->println(F("No se pudo generar QR"));
  } else {
    display->printf("Red: %s", AP_SSID);
  }
  display->setCursor(5, provisioningQrBottom + 17);
  display->printf("Clave: %s", AP_PASSWORD);
  display->setCursor(5, provisioningQrBottom + 29);
  if (dnsServerReady) {
    display->println(F("Abre el aviso de conexion"));
  } else {
    display->println(F("DNS no disponible; abre:"));
  }
  display->setCursor(5, provisioningQrBottom + 41);
  display->println(F("http://192.168.4.1"));
  display->setCursor(5, provisioningQrBottom + 53);
  display->println(F("BOOT: configurar por BLE"));
}

void showStaPortalQrScreen() {
  display->fillScreen(RGB565_WHITE);
  display->setTextWrap(false);
  display->setTextColor(RGB565_BLACK);
  display->setTextSize(1);
  display->setCursor(8, 5);
  display->println(F("ABRIR PORTAL STA"));
  display->setCursor(8, 19);
  display->println(F("Escanea para abrir navegador"));

  if (WiFi.status() != WL_CONNECTED) {
    display->setCursor(8, 52);
    display->println(F("STA sin conexion."));
    display->setCursor(8, 70);
    display->println(F("Configura Wi-Fi con el"));
    display->setCursor(8, 84);
    display->println(F("QR anterior y vuelve aqui."));
    display->setCursor(8, 112);
    display->println(F("BOOT: volver al estado"));
    return;
  }

  String url = F("http://");
  url += WiFi.localIP().toString();
  url += '/';

  esp_qrcode_config_t qrConfig = ESP_QRCODE_CONFIG_DEFAULT();
  qrConfig.display_func = drawQrModules;
  qrConfig.max_qrcode_version = 6;
  provisioningQrBottom = 34;
  const esp_err_t result = esp_qrcode_generate(&qrConfig, url.c_str());

  display->setTextSize(1);
  display->setCursor(8, provisioningQrBottom + 6);
  if (result != ESP_OK) {
    display->println(F("No se pudo generar QR"));
  } else {
    display->println(url);
  }
  display->setCursor(8, provisioningQrBottom + 24);
  display->println(F("BOOT: volver al estado"));
}

void showSdCardScreen() {
  display->fillScreen(RGB565_BLACK);
  display->setTextWrap(false);
  display->setTextColor(RGB565_CYAN);
  display->setTextSize(1);
  display->setCursor(8, 8);
  display->println(F("ESTADO MICROSD"));
  display->drawFastHLine(8, 23, 156, RGB565_DARKGREY);
  display->setCursor(8, 38);
  display->setTextColor(sdCardReady ? RGB565_GREEN : RGB565_YELLOW);
  display->println(sdCardReady ? F("Estado: MONTADA") : F("Estado: NO MONTADA"));
  display->setTextColor(RGB565_WHITE);
  display->setCursor(8, 58);
  if (sdCardReady) {
    display->printf("Tamano: %llu MB",
                    static_cast<unsigned long long>(SD_MMC.cardSize() /
                                                    (1024ULL * 1024ULL)));
    display->setCursor(8, 74);
    display->printf("Usados: %llu MB",
                    static_cast<unsigned long long>(SD_MMC.usedBytes() /
                                                    (1024ULL * 1024ULL)));
  } else {
    display->println(F("Inserta la tarjeta y reinicia"));
    display->setCursor(8, 74);
    display->println(F("si no aparece detectada."));
    display->setCursor(8, 94);
    display->println(sdLastMessage.substring(0, 25));
  }
  display->setCursor(8, 132);
  display->setTextColor(RGB565_ORANGE);
  display->println(F("BOOT para volver al estado"));
}

bool formatAndMountSdCard() {
  sdCardReady = false;
  SD_MMC.end();
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) {
    sdLastMessage = "No se pudieron configurar pines SDMMC";
    Serial.println(sdLastMessage);
    return false;
  }

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.max_freq_khz = SDMMC_FREQ_PROBING;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = static_cast<gpio_num_t>(PIN_SD_CLK);
  slot.cmd = static_cast<gpio_num_t>(PIN_SD_CMD);
  slot.d0 = static_cast<gpio_num_t>(PIN_SD_D0);
  slot.d1 = GPIO_NUM_NC;
  slot.d2 = GPIO_NUM_NC;
  slot.d3 = GPIO_NUM_NC;
  slot.width = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_mount_config_t mountConfig = {
      .format_if_mount_failed = true,
      .max_files = 2,
      .allocation_unit_size = 0,
      .disk_status_check_enable = false,
      .use_one_fat = false,
  };
  sdmmc_card_t *card = nullptr;
  const esp_err_t mountResult =
      esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mountConfig, &card);
  if (mountResult != ESP_OK) {
    sdLastMessage = String("Inicializacion SD: ") + esp_err_to_name(mountResult);
    recordError("microSD", sdLastMessage);
    Serial.printf("ERROR: %s (0x%x)\n", sdLastMessage.c_str(),
                  static_cast<unsigned>(mountResult));
    if (mountResult == ESP_ERR_INVALID_RESPONSE) {
      Serial.println(F("La tarjeta no respondio al bus SDMMC; no se puede formatear."));
    }
    return false;
  }

  Serial.printf("MicroSD detectada: %llu MB; formateando FAT\n",
                static_cast<unsigned long long>(card->csd.capacity) /
                    (1024ULL * 1024ULL));
  const esp_err_t formatResult = esp_vfs_fat_sdcard_format("/sdcard", card);
  const esp_err_t unmountResult = esp_vfs_fat_sdcard_unmount("/sdcard", card);
  if (formatResult != ESP_OK) {
    sdLastMessage = String("Formato FAT: ") + esp_err_to_name(formatResult);
    recordError("microSD", sdLastMessage);
    Serial.printf("ERROR: %s (0x%x)\n", sdLastMessage.c_str(),
                  static_cast<unsigned>(formatResult));
    return false;
  }
  if (unmountResult != ESP_OK) {
    sdLastMessage = String("Desmontaje: ") + esp_err_to_name(unmountResult);
    recordError("microSD", sdLastMessage);
    Serial.printf("ERROR: %s (0x%x)\n", sdLastMessage.c_str(),
                  static_cast<unsigned>(unmountResult));
    return false;
  }

  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0) ||
      !SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_PROBING) ||
      SD_MMC.cardType() == CARD_NONE) {
    sdLastMessage = "FAT creado, pero no se pudo montar";
    recordError("microSD", sdLastMessage);
    Serial.println(sdLastMessage);
    SD_MMC.end();
    return false;
  }

  sdCardReady = true;
  sdLastMessage = "FAT formateada; microSD montada";
  Serial.println(sdLastMessage);
  return true;
}

void sdFormatTask(void *) {
  sdFormatSucceeded = formatAndMountSdCard();
  sdFormatRunning = false;
  vTaskDelete(nullptr);
}

bool startSdFormatTask() {
  if (sdFormatRunning) {
    return false;
  }
  sdFormatSucceeded = false;
  sdFormatAttempted = true;
  sdLastMessage = "Formateando microSD";
  sdFormatRunning = true;
  if (xTaskCreatePinnedToCore(sdFormatTask, "sd-format", 12288, nullptr, 1,
                              nullptr, 1) != pdPASS) {
    sdFormatRunning = false;
    sdLastMessage = "No se pudo iniciar la tarea de formato";
    return false;
  }
  return true;
}

void handleBootButton() {
  static bool lastReading = HIGH;
  static bool stableReading = HIGH;
  static uint32_t lastChangeAt = 0;
  static uint32_t pressStartedAt = 0;
  static constexpr uint32_t LONG_PRESS_MS = 900;
  const bool reading = digitalRead(PIN_BOOT);
  if (reading != lastReading) {
    lastReading = reading;
    lastChangeAt = millis();
  }
  if (millis() - lastChangeAt < 40 || reading == stableReading) {
    return;
  }
  stableReading = reading;
  if (stableReading == LOW) {
    pressStartedAt = millis();
    return;
  }

  const bool longPress = millis() - pressStartedAt >= LONG_PRESS_MS;
  if (longPress) {
    otaConfigAuthorizationUntil = millis() + 30000UL;
    otaConfigAuthorized = true;
    if (bootQrScreen == BootQrScreen::SdCard) {
      bootQrScreen = BootQrScreen::Status;
      updateDisplay();
    } else {
      bootQrScreen = BootQrScreen::SdCard;
      showSdCardScreen();
    }
    return;
  }

  switch (bootQrScreen) {
  case BootQrScreen::Status:
    bootQrScreen = BootQrScreen::AccessPointPortal;
    showAccessPointPortalScreen();
    break;
  case BootQrScreen::AccessPointPortal:
    bootQrScreen = BootQrScreen::WifiProvisioning;
    startBleProvisioning(true);
    showProvisioningQrScreen();
    break;
  case BootQrScreen::WifiProvisioning:
    bootQrScreen = BootQrScreen::StaPortal;
    showStaPortalQrScreen();
    break;
  case BootQrScreen::StaPortal:
    bootQrScreen = BootQrScreen::Status;
    updateDisplay();
    break;
  case BootQrScreen::SdCard:
    bootQrScreen = BootQrScreen::Status;
    updateDisplay();
    break;
  }
}

void handleScan() {
  if (!requireSession()) {
    return;
  }
  if (!scanNearbyNetworks()) {
    sendError(500, "Falló el escaneo pasivo de Wi-Fi");
    return;
  }
  sendJson(200, scanResultsJson());
}

void handleConnect() {
  if (!requireSession()) {
    return;
  }
  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 64) {
    sendError(400, "SSID o contraseña no válidos");
    return;
  }
  startStaConnection({ssid, password}, true);
  sendJson(202, "{\"ok\":true,\"message\":\"Intentando conectar\"}");
}

void handleSdFormat() {
  if (!requireSession()) {
    return;
  }
  if (server.arg("confirm") != "FORMATEAR") {
    sendError(400, "Confirma el formato desde la página Resumen");
    return;
  }
  if (sdFormatRunning) {
    sendError(409, "Ya hay un formateo en curso");
    return;
  }
  if (!startSdFormatTask()) {
    sendError(503, "No se pudo iniciar la tarea de formato");
    return;
  }
  sendJson(202, "{\"ok\":true,\"message\":\"Formato de microSD iniciado\"}");
}

void handlePing() {
  if (!requireSession()) {
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(204, "text/plain", "");
}

void handleGatewayPing() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a una red para probar su puerta de enlace");
    return;
  }

  const uint32_t now = millis();
  if (gatewayPingAttempted && now - lastGatewayPingAt < 3000) {
    sendError(429, "Espera unos segundos antes de repetir la prueba");
    return;
  }

  const IPAddress gateway = WiFi.gatewayIP();
  if (gateway == IPAddress(0, 0, 0, 0)) {
    sendError(503, "La red conectada no informó una puerta de enlace");
    return;
  }

  gatewayPingAttempted = true;
  lastGatewayPingAt = now;
  const bool reachable = Ping.ping(gateway, 1);
  String body = F("{\"target\":\"");
  body += gateway.toString();
  body += F("\",\"reachable\":");
  body += reachable ? F("true") : F("false");
  body += F(",\"averageMs\":");
  if (reachable) {
    body += String(Ping.averageTime(), 1);
  } else {
    body += F("null");
  }
  body += F(",\"rssi\":");
  body += String(WiFi.RSSI());
  body += '}';
  sendJson(200, body);
}

bool isStaSubnetUnicastTarget(const IPAddress &target) {
  const IPAddress local = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  bool sameSubnet = true;
  bool networkAddress = true;
  bool broadcastAddress = true;
  for (size_t i = 0; i < 4; ++i) {
    const uint8_t networkOctet = local[i] & mask[i];
    sameSubnet &= (target[i] & mask[i]) == networkOctet;
    networkAddress &= target[i] == networkOctet;
    broadcastAddress &=
        target[i] == static_cast<uint8_t>(networkOctet | ~mask[i]);
  }
  return sameSubnet && !networkAddress && !broadcastAddress &&
         target[0] < 224 && target[0] != 127 && target != local;
}

String sadpXmlTagValue(const String &xml, const char *tag, size_t maxLength) {
  String openTag = F("<");
  openTag += tag;
  openTag += '>';
  String closeTag = F("</");
  closeTag += tag;
  closeTag += '>';
  const int valueStart = xml.indexOf(openTag);
  if (valueStart < 0) {
    return "";
  }
  const int contentStart = valueStart + openTag.length();
  const int contentEnd = xml.indexOf(closeTag, contentStart);
  if (contentEnd < 0) {
    return "";
  }
  String value = xml.substring(contentStart, contentEnd);
  value.trim();
  return sanitizeAssetValue(value, maxLength);
}

bool parseSadpResponse(const char *payload, IPAddress sender,
                       SadpDevice &device) {
  const String xml(payload);
  if (xml.indexOf("<ProbeMatch") < 0 || xml.indexOf("<Device") < 0) {
    return false;
  }

  IPAddress deviceIp;
  const String ipValue = sadpXmlTagValue(xml, "IPv4Address", 15);
  if (!deviceIp.fromString(ipValue)) {
    deviceIp = sender;
  }
  if (!isStaSubnetUnicastTarget(deviceIp)) {
    return false;
  }

  String normalizedMac;
  const String macValue = sadpXmlTagValue(xml, "MAC", 17);
  if (!macValue.isEmpty() && !normalizeMacAddress(macValue, normalizedMac)) {
    normalizedMac = "";
  }
  String model = sadpXmlTagValue(xml, "DeviceType", 48);
  if (model.isEmpty()) {
    model = sadpXmlTagValue(xml, "DeviceDescription", 48);
  }
  device.ip = deviceIp.toString();
  device.mac = normalizedMac;
  device.model = model;
  device.firmware = sadpXmlTagValue(xml, "SoftwareVersion", 32);
  return true;
}

void addSadpDevice(const SadpDevice &device) {
  for (SadpDevice &existing : sadpDevices) {
    if (existing.ip == device.ip ||
        (!device.mac.isEmpty() && existing.mac == device.mac)) {
      if (existing.mac.isEmpty()) {
        existing.mac = device.mac;
      }
      if (existing.model.isEmpty()) {
        existing.model = device.model;
      }
      if (existing.firmware.isEmpty()) {
        existing.firmware = device.firmware;
      }
      return;
    }
  }
  if (sadpDevices.size() < MAX_SADP_DEVICES) {
    sadpDevices.push_back(device);
  }
}

String sadpDevicesJson(const DiscoveryHistorySaveStatus &saveStatus) {
  String body;
  body.reserve(128 + sadpDevices.size() * 180);
  body = F("{\"ok\":true,\"count\":");
  body += String(sadpDevices.size());
  body += F(",\"devices\":[");
  for (size_t i = 0; i < sadpDevices.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    const SadpDevice &device = sadpDevices[i];
    body += F("{\"ip\":\"");
    body += jsonEscape(device.ip);
    body += F("\",\"mac\":\"");
    body += jsonEscape(device.mac);
    body += F("\",\"model\":\"");
    body += jsonEscape(device.model);
    body += F("\",\"firmware\":\"");
    body += jsonEscape(device.firmware);
    body += F("\"}");
  }
  body += F("]");
  appendDiscoveryHistorySaveStatus(body, saveStatus);
  body += '}';
  return body;
}

void handleSadpDiscovery() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a la LAN antes de buscar dispositivos SADP");
    return;
  }

  sadpDevices.clear();
  WiFiUDP udp;
  if (!udp.beginMulticast(SADP_MULTICAST_IP, SADP_PORT)) {
    sendError(503, "No se pudo abrir UDP multicast para SADP");
    return;
  }

  char uuid[37];
  const uint32_t randomA = esp_random();
  const uint32_t randomB = esp_random();
  const uint32_t randomC = esp_random();
  const uint32_t randomD = esp_random();
  snprintf(uuid, sizeof(uuid), "%08lX-%04X-%04X-%04X-%012llX",
           static_cast<unsigned long>(randomA),
           static_cast<unsigned>((randomB >> 16) & 0xFFFF),
           static_cast<unsigned>(randomB & 0xFFFF),
           static_cast<unsigned>((randomC >> 16) & 0xFFFF),
           static_cast<unsigned long long>(
               (static_cast<uint64_t>(randomC & 0xFFFF) << 32) | randomD));
  String probe = F("<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                   "<Probe><Uuid>");
  probe += uuid;
  probe += F("</Uuid><Types>inquiry</Types></Probe>");
  if (udp.beginPacket(SADP_MULTICAST_IP, SADP_PORT) != 1 ||
      udp.write(reinterpret_cast<const uint8_t *>(probe.c_str()),
                probe.length()) != probe.length() ||
      udp.endPacket() != 1) {
    udp.stop();
    sendError(503, "No se pudo enviar la consulta multicast SADP");
    return;
  }

  uint32_t receivedPackets = 0;
  const uint32_t startedAt = millis();
  char packet[2048];
  while (millis() - startedAt < SADP_LISTEN_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(10);
      continue;
    }
    ++receivedPackets;
    const IPAddress sender = udp.remoteIP();
    if (!isStaSubnetUnicastTarget(sender) ||
        static_cast<size_t>(packetSize) >= sizeof(packet)) {
      while (udp.available()) {
        udp.read();
      }
      continue;
    }
    const int bytesRead = udp.read(packet, static_cast<size_t>(packetSize));
    while (udp.available()) {
      udp.read();
    }
    if (bytesRead <= 0) {
      continue;
    }
    packet[bytesRead] = '\0';
    SadpDevice device;
    if (parseSadpResponse(packet, sender, device)) {
      addSadpDevice(device);
    }
  }
  udp.stop();
  Serial.printf("[SADP] %u paquetes recibidos, %u dispositivos compatibles\n",
                static_cast<unsigned>(receivedPackets),
                static_cast<unsigned>(sadpDevices.size()));
  std::vector<DiscoveryHistoryRecord> records;
  records.reserve(sadpDevices.size());
  for (const SadpDevice &device : sadpDevices) {
    DiscoveryHistoryRecord record{};
    record.source = F("SADP");
    record.ip = device.ip;
    record.protocol = F("SADP");
    record.identity = device.model;
    record.mac = device.mac;
    record.model = device.model;
    record.firmware = device.firmware;
    records.push_back(record);
  }
  const DiscoveryHistorySaveStatus saveStatus =
      saveDiscoveryHistory(records, server.arg("recordDate"),
                           server.arg("recordTime"));
  sendJson(200, sadpDevicesJson(saveStatus));
}

bool parseDhJsonString(const String &json, size_t &cursor, String &value,
                       size_t maxLength) {
  if (cursor >= json.length() || json[cursor] != '"') {
    return false;
  }
  ++cursor;
  value = "";
  value.reserve(maxLength);
  while (cursor < json.length()) {
    char character = json[cursor++];
    if (character == '"') {
      return true;
    }
    if (character == '\\') {
      if (cursor >= json.length()) {
        return false;
      }
      const char escaped = json[cursor++];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
        character = escaped;
        break;
      case 'b':
        character = '\b';
        break;
      case 'f':
        character = '\f';
        break;
      case 'n':
        character = '\n';
        break;
      case 'r':
        character = '\r';
        break;
      case 't':
        character = '\t';
        break;
      case 'u': {
        if (json.length() - cursor < 4) {
          return false;
        }
        uint16_t codePoint = 0;
        for (size_t i = 0; i < 4; ++i) {
          const char digit = json[cursor++];
          uint8_t valueNibble;
          if (digit >= '0' && digit <= '9') {
            valueNibble = static_cast<uint8_t>(digit - '0');
          } else if (digit >= 'a' && digit <= 'f') {
            valueNibble = static_cast<uint8_t>(digit - 'a' + 10);
          } else if (digit >= 'A' && digit <= 'F') {
            valueNibble = static_cast<uint8_t>(digit - 'A' + 10);
          } else {
            return false;
          }
          codePoint = static_cast<uint16_t>((codePoint << 4) | valueNibble);
        }
        character = codePoint < 0x80 ? static_cast<char>(codePoint) : '?';
        break;
      }
      default:
        return false;
      }
    } else if (static_cast<uint8_t>(character) < 0x20) {
      return false;
    }
    if (value.length() < maxLength) {
      value += character;
    }
  }
  return false;
}

bool dhJsonStringField(const String &json, const char *field,
                       size_t maxLength, String &value) {
  size_t cursor = 0;
  while (cursor < json.length()) {
    if (json[cursor] != '"') {
      ++cursor;
      continue;
    }
    String key;
    if (!parseDhJsonString(json, cursor, key, 64)) {
      return false;
    }
    size_t valueStart = cursor;
    while (valueStart < json.length() &&
           std::isspace(static_cast<unsigned char>(json[valueStart]))) {
      ++valueStart;
    }
    if (key != field || valueStart >= json.length() ||
        json[valueStart] != ':') {
      continue;
    }
    ++valueStart;
    while (valueStart < json.length() &&
           std::isspace(static_cast<unsigned char>(json[valueStart]))) {
      ++valueStart;
    }
    if (valueStart >= json.length() || json[valueStart] != '"') {
      continue;
    }
    String rawValue;
    if (!parseDhJsonString(json, valueStart, rawValue, maxLength)) {
      return false;
    }
    value = sanitizeAssetValue(rawValue, maxLength);
    return true;
  }
  return false;
}

bool parseDhDiscoverResponse(const uint8_t *payload, size_t payloadLength,
                             IPAddress sender, DhDiscoverDevice &device) {
  if (payloadLength <= 32 || payload[4] != 'D' || payload[5] != 'H' ||
      payload[6] != 'I' || payload[7] != 'P') {
    return false;
  }

  size_t jsonStart = 8;
  while (jsonStart < payloadLength && payload[jsonStart] != '{') {
    ++jsonStart;
  }
  if (jsonStart == payloadLength) {
    return false;
  }

  String json;
  json.reserve(payloadLength - jsonStart);
  for (size_t i = jsonStart; i < payloadLength && payload[i] != '\0'; ++i) {
    json += static_cast<char>(payload[i]);
  }

  String deviceType;
  String deviceClass;
  String serial;
  String macValue;
  String model;
  String firmware;
  String ipValue;
  dhJsonStringField(json, "DeviceType", 48, deviceType);
  dhJsonStringField(json, "DeviceClass", 24, deviceClass);
  dhJsonStringField(json, "SerialNo", 32, serial);
  if (!dhJsonStringField(json, "Mac", 17, macValue)) {
    dhJsonStringField(json, "mac", 17, macValue);
  }
  if (deviceType.isEmpty()) {
    dhJsonStringField(json, "Model", 48, deviceType);
  }
  if (deviceType.isEmpty()) {
    deviceType = deviceClass;
  }
  if (!dhJsonStringField(json, "Version", 32, firmware) ||
      firmware.isEmpty()) {
    dhJsonStringField(json, "SoftwareVersion", 32, firmware);
  }
  if (!dhJsonStringField(json, "IPAddress", 15, ipValue) ||
      ipValue.isEmpty()) {
    dhJsonStringField(json, "IPv4Address", 15, ipValue);
  }
  if (deviceType.isEmpty() && deviceClass.isEmpty() && serial.isEmpty()) {
    return false;
  }

  IPAddress deviceIp;
  if (!deviceIp.fromString(ipValue)) {
    deviceIp = sender;
  }
  if (!isStaSubnetUnicastTarget(deviceIp)) {
    return false;
  }

  String normalizedMac;
  if (!macValue.isEmpty() && !normalizeMacAddress(macValue, normalizedMac)) {
    normalizedMac = "";
  }
  device.ip = deviceIp.toString();
  device.mac = normalizedMac;
  device.model = deviceType;
  device.deviceClass = deviceClass;
  device.firmware = firmware;
  device.serial = serial;
  return true;
}

void addDhDiscoverDevice(const DhDiscoverDevice &device) {
  for (DhDiscoverDevice &existing : dhDiscoverDevices) {
    if (existing.ip == device.ip ||
        (!device.mac.isEmpty() && existing.mac == device.mac) ||
        (!device.serial.isEmpty() && existing.serial == device.serial)) {
      if (existing.mac.isEmpty()) {
        existing.mac = device.mac;
      }
      if (existing.model.isEmpty()) {
        existing.model = device.model;
      }
      if (existing.deviceClass.isEmpty()) {
        existing.deviceClass = device.deviceClass;
      }
      if (existing.firmware.isEmpty()) {
        existing.firmware = device.firmware;
      }
      if (existing.serial.isEmpty()) {
        existing.serial = device.serial;
      }
      return;
    }
  }
  if (dhDiscoverDevices.size() < MAX_DH_DISCOVER_DEVICES) {
    dhDiscoverDevices.push_back(device);
  }
}

String dhDiscoverDevicesJson(
    const DiscoveryHistorySaveStatus &saveStatus) {
  String body;
  body.reserve(160 + dhDiscoverDevices.size() * 240);
  body = F("{\"ok\":true,\"count\":");
  body += String(dhDiscoverDevices.size());
  body += F(",\"devices\":[");
  for (size_t i = 0; i < dhDiscoverDevices.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    const DhDiscoverDevice &device = dhDiscoverDevices[i];
    body += F("{\"ip\":\"");
    body += jsonEscape(device.ip);
    body += F("\",\"mac\":\"");
    body += jsonEscape(device.mac);
    body += F("\",\"model\":\"");
    body += jsonEscape(device.model);
    body += F("\",\"deviceClass\":\"");
    body += jsonEscape(device.deviceClass);
    body += F("\",\"firmware\":\"");
    body += jsonEscape(device.firmware);
    body += F("\",\"serial\":\"");
    body += jsonEscape(device.serial);
    body += F("\"}");
  }
  body += F("]");
  appendDiscoveryHistorySaveStatus(body, saveStatus);
  body += '}';
  return body;
}

void handleDhDiscover() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a la LAN antes de buscar dispositivos Dahua");
    return;
  }

  dhDiscoverDevices.clear();
  WiFiUDP udp;
  if (!udp.beginMulticast(DH_DISCOVER_MULTICAST_IP, DH_DISCOVER_PORT)) {
    sendError(503, "No se pudo abrir UDP multicast para DHDiscover");
    return;
  }

  String probe =
      F("{\"method\":\"DHDiscover.search\",\"params\":{\"mac\":\"\",\"uni\":1}}");
  // DHIP uses a 32-byte header with the ASCII magic and little-endian body lengths.
  uint8_t header[32] = {0x20, 0, 0, 0, 'D', 'H', 'I', 'P'};
  const uint32_t payloadLength = probe.length();
  for (size_t i = 0; i < 4; ++i) {
    const uint8_t lengthByte =
        static_cast<uint8_t>((payloadLength >> (i * 8)) & 0xFF);
    header[16 + i] = lengthByte;
    header[24 + i] = lengthByte;
  }

  if (udp.beginPacket(DH_DISCOVER_MULTICAST_IP, DH_DISCOVER_PORT) != 1 ||
      udp.write(header, sizeof(header)) != sizeof(header) ||
      udp.write(reinterpret_cast<const uint8_t *>(probe.c_str()),
                probe.length()) != probe.length() ||
      udp.endPacket() != 1) {
    udp.stop();
    sendError(503, "No se pudo enviar la consulta multicast DHDiscover");
    return;
  }

  uint32_t receivedPackets = 0;
  const uint32_t startedAt = millis();
  uint8_t packet[2048];
  while (millis() - startedAt < DH_DISCOVER_LISTEN_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(10);
      continue;
    }
    ++receivedPackets;
    const IPAddress sender = udp.remoteIP();
    if (!isStaSubnetUnicastTarget(sender) ||
        static_cast<size_t>(packetSize) > sizeof(packet)) {
      while (udp.available()) {
        udp.read();
      }
      continue;
    }
    const int bytesRead = udp.read(packet, static_cast<size_t>(packetSize));
    while (udp.available()) {
      udp.read();
    }
    if (bytesRead <= 0) {
      continue;
    }
    DhDiscoverDevice device;
    if (parseDhDiscoverResponse(packet, static_cast<size_t>(bytesRead), sender,
                                device)) {
      addDhDiscoverDevice(device);
    }
  }
  udp.stop();
  Serial.printf(
      "[DHDiscover] %u paquetes recibidos, %u dispositivos compatibles\n",
      static_cast<unsigned>(receivedPackets),
      static_cast<unsigned>(dhDiscoverDevices.size()));
  std::vector<DiscoveryHistoryRecord> records;
  records.reserve(dhDiscoverDevices.size());
  for (const DhDiscoverDevice &device : dhDiscoverDevices) {
    DiscoveryHistoryRecord record{};
    record.source = F("DHDiscover");
    record.ip = device.ip;
    record.protocol = F("DHDiscover");
    record.identity = device.model;
    record.mac = device.mac;
    record.model = device.model;
    record.deviceClass = device.deviceClass;
    record.firmware = device.firmware;
    record.serial = device.serial;
    records.push_back(record);
  }
  const DiscoveryHistorySaveStatus saveStatus =
      saveDiscoveryHistory(records, server.arg("recordDate"),
                           server.arg("recordTime"));
  sendJson(200, dhDiscoverDevicesJson(saveStatus));
}

void addGenericDiscoveryDevice(IPAddress sender, const String &protocol,
                               const String &identity,
                               const String &endpoint) {
  if (!isStaSubnetUnicastTarget(sender) ||
      (identity.isEmpty() && endpoint.isEmpty())) {
    return;
  }
  const String ip = sender.toString();
  for (GenericDiscoveryDevice &existing : genericDiscoveryDevices) {
    if (existing.ip == ip && existing.protocol == protocol) {
      if (existing.identity.isEmpty()) {
        existing.identity = identity;
      } else if (!identity.isEmpty() &&
                 existing.identity.indexOf(identity) < 0) {
        existing.identity += F(", ");
        existing.identity += identity;
      }
      if (existing.endpoint.isEmpty()) {
        existing.endpoint = endpoint;
      }
      return;
    }
  }
  if (genericDiscoveryDevices.size() < MAX_GENERIC_DEVICES) {
    genericDiscoveryDevices.push_back(
        {ip, protocol, sanitizeAssetValue(identity, 96),
         sanitizeAssetValue(endpoint, 160)});
  }
}

String genericDiscoveryDevicesJson(
    const DiscoveryHistorySaveStatus &saveStatus) {
  String body;
  body.reserve(128 + genericDiscoveryDevices.size() * 260);
  body = F("{\"ok\":true,\"count\":");
  body += String(genericDiscoveryDevices.size());
  body += F(",\"devices\":[");
  for (size_t i = 0; i < genericDiscoveryDevices.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    const GenericDiscoveryDevice &device = genericDiscoveryDevices[i];
    body += F("{\"ip\":\"");
    body += jsonEscape(device.ip);
    body += F("\",\"protocol\":\"");
    body += jsonEscape(device.protocol);
    body += F("\",\"identity\":\"");
    body += jsonEscape(device.identity);
    body += F("\",\"endpoint\":\"");
    body += jsonEscape(device.endpoint);
    body += F("\"}");
  }
  body += F("]");
  appendDiscoveryHistorySaveStatus(body, saveStatus);
  body += '}';
  return body;
}

String genericXmlTagValue(const String &xml, const char *tag,
                          size_t maxLength) {
  int searchFrom = 0;
  while (searchFrom < static_cast<int>(xml.length())) {
    const int tagPosition = xml.indexOf(tag, searchFrom);
    if (tagPosition < 0) {
      return "";
    }
    const int openStart = xml.lastIndexOf('<', tagPosition);
    const int openEnd = xml.indexOf('>', tagPosition);
    if (openStart < 0 || openEnd < 0 || openStart + 1 >= openEnd ||
        xml[openStart + 1] == '/') {
      searchFrom = tagPosition + strlen(tag);
      continue;
    }
    String qualifiedName = xml.substring(openStart + 1, openEnd);
    const int namespaceEnd = qualifiedName.lastIndexOf(':');
    if (namespaceEnd >= 0) {
      qualifiedName = qualifiedName.substring(namespaceEnd + 1);
    }
    if (qualifiedName != tag) {
      searchFrom = tagPosition + strlen(tag);
      continue;
    }
    const int valueStart = openEnd + 1;
    const int valueEnd = xml.indexOf('<', valueStart);
    if (valueEnd < 0) {
      return "";
    }
    return sanitizeAssetValue(xml.substring(valueStart, valueEnd), maxLength);
  }
  return "";
}

bool parseGenericOnvifResponse(const char *payload, IPAddress sender) {
  if (!isStaSubnetUnicastTarget(sender)) {
    return false;
  }
  const String xml(payload);
  if (xml.indexOf("ProbeMatch") < 0) {
    return false;
  }
  String identity = genericXmlTagValue(xml, "Types", 96);
  if (identity.isEmpty()) {
    identity = genericXmlTagValue(xml, "Scopes", 96);
  }
  String endpoint = genericXmlTagValue(xml, "XAddrs", 160);
  const int separator = endpoint.indexOf(' ');
  if (separator >= 0) {
    endpoint = endpoint.substring(0, separator);
  }
  if (identity.isEmpty() && endpoint.isEmpty()) {
    return false;
  }
  addGenericDiscoveryDevice(sender, F("ONVIF/WS-Discovery"), identity,
                            endpoint);
  return true;
}

String ssdpHeaderValue(const String &response, const char *header) {
  String wanted = header;
  wanted.toLowerCase();
  size_t lineStart = 0;
  while (lineStart < response.length()) {
    const int lineEnd = response.indexOf('\n', lineStart);
    const size_t end = lineEnd < 0 ? response.length()
                                   : static_cast<size_t>(lineEnd);
    String line = response.substring(lineStart, end);
    line.trim();
    const int colon = line.indexOf(':');
    if (colon >= 0) {
      String name = line.substring(0, colon);
      name.trim();
      name.toLowerCase();
      if (name == wanted) {
        return sanitizeAssetValue(line.substring(colon + 1), 160);
      }
    }
    if (lineEnd < 0) {
      break;
    }
    lineStart = static_cast<size_t>(lineEnd) + 1;
  }
  return "";
}

bool parseGenericSsdpResponse(const char *payload, IPAddress sender) {
  if (!isStaSubnetUnicastTarget(sender)) {
    return false;
  }
  String response(payload);
  String statusLine = response;
  statusLine.trim();
  statusLine.toLowerCase();
  if (!statusLine.startsWith("http/")) {
    return false;
  }
  String identity = ssdpHeaderValue(response, "SERVER");
  if (identity.isEmpty()) {
    identity = ssdpHeaderValue(response, "ST");
  }
  if (identity.isEmpty()) {
    identity = ssdpHeaderValue(response, "USN");
  }
  const String endpoint = ssdpHeaderValue(response, "LOCATION");
  if (identity.isEmpty() && endpoint.isEmpty()) {
    return false;
  }
  addGenericDiscoveryDevice(sender, F("SSDP/UPnP"), identity, endpoint);
  return true;
}

bool collectGenericOnvifResponses() {
  WiFiUDP udp;
  if (!udp.beginMulticast(GENERIC_WSD_MULTICAST_IP, GENERIC_WSD_PORT)) {
    return false;
  }
  char uuid[37];
  const uint32_t randomA = esp_random();
  const uint32_t randomB = esp_random();
  const uint32_t randomC = esp_random();
  const uint32_t randomD = esp_random();
  snprintf(uuid, sizeof(uuid), "%08lX-%04X-%04X-%04X-%012llX",
           static_cast<unsigned long>(randomA),
           static_cast<unsigned>((randomB >> 16) & 0xFFFF),
           static_cast<unsigned>(randomB & 0xFFFF),
           static_cast<unsigned>((randomC >> 16) & 0xFFFF),
           static_cast<unsigned long long>(
               (static_cast<uint64_t>(randomC & 0xFFFF) << 32) | randomD));
  String probe =
      F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
        "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
        "<s:Header><a:Action>http://schemas.xmlsoap.org/ws/2005/04/"
        "discovery/Probe</a:Action><a:MessageID>uuid:");
  probe += uuid;
  probe +=
      F("</a:MessageID><a:To>urn:schemas-xmlsoap-org:ws:2005:04:"
        "discovery</a:To></s:Header><s:Body><d:Probe><d:Types>"
        "dn:NetworkVideoTransmitter</d:Types></d:Probe></s:Body></s:Envelope>");
  if (udp.beginPacket(GENERIC_WSD_MULTICAST_IP, GENERIC_WSD_PORT) != 1 ||
      udp.write(reinterpret_cast<const uint8_t *>(probe.c_str()),
                probe.length()) != probe.length() ||
      udp.endPacket() != 1) {
    udp.stop();
    return false;
  }

  const uint32_t startedAt = millis();
  char packet[2048];
  while (millis() - startedAt < GENERIC_DISCOVERY_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(10);
      continue;
    }
    const IPAddress sender = udp.remoteIP();
    if (!isStaSubnetUnicastTarget(sender) ||
        static_cast<size_t>(packetSize) >= sizeof(packet)) {
      while (udp.available()) {
        udp.read();
      }
      continue;
    }
    const int bytesRead = udp.read(packet, static_cast<size_t>(packetSize));
    while (udp.available()) {
      udp.read();
    }
    if (bytesRead > 0) {
      packet[bytesRead] = '\0';
      parseGenericOnvifResponse(packet, sender);
    }
  }
  udp.stop();
  return true;
}

bool collectGenericSsdpResponses() {
  WiFiUDP udp;
  if (!udp.beginMulticast(GENERIC_SSDP_MULTICAST_IP, GENERIC_SSDP_PORT)) {
    return false;
  }
  const String probe =
      F("M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: ssdp:all\r\n\r\n");
  if (udp.beginPacket(GENERIC_SSDP_MULTICAST_IP, GENERIC_SSDP_PORT) != 1 ||
      udp.write(reinterpret_cast<const uint8_t *>(probe.c_str()),
                probe.length()) != probe.length() ||
      udp.endPacket() != 1) {
    udp.stop();
    return false;
  }

  const uint32_t startedAt = millis();
  char packet[2048];
  while (millis() - startedAt < GENERIC_DISCOVERY_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(10);
      continue;
    }
    const IPAddress sender = udp.remoteIP();
    if (!isStaSubnetUnicastTarget(sender) ||
        static_cast<size_t>(packetSize) >= sizeof(packet)) {
      while (udp.available()) {
        udp.read();
      }
      continue;
    }
    const int bytesRead = udp.read(packet, static_cast<size_t>(packetSize));
    while (udp.available()) {
      udp.read();
    }
    if (bytesRead > 0) {
      packet[bytesRead] = '\0';
      parseGenericSsdpResponse(packet, sender);
    }
  }
  udp.stop();
  return true;
}

uint16_t readBigEndian16(const uint8_t *data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               data[1]);
}

bool readMdnsName(const uint8_t *packet, size_t packetLength, size_t &cursor,
                  String &name) {
  size_t position = cursor;
  size_t resumePosition = 0;
  bool jumped = false;
  uint8_t jumps = 0;
  name = "";
  while (position < packetLength) {
    const uint8_t labelLength = packet[position++];
    if (labelLength == 0) {
      cursor = jumped ? resumePosition : position;
      return true;
    }
    if ((labelLength & 0xC0) == 0xC0) {
      if (position >= packetLength || jumps++ >= 16) {
        return false;
      }
      const size_t offset =
          (static_cast<size_t>(labelLength & 0x3F) << 8) | packet[position++];
      if (offset >= packetLength) {
        return false;
      }
      if (!jumped) {
        resumePosition = position;
        jumped = true;
      }
      position = offset;
      continue;
    }
    if ((labelLength & 0xC0) != 0 || labelLength > 63 ||
        labelLength > packetLength - position) {
      return false;
    }
    if (!name.isEmpty() && name.length() < 255) {
      name += '.';
    }
    for (size_t i = 0; i < labelLength; ++i) {
      if (name.length() < 255) {
        name += static_cast<char>(
            std::tolower(static_cast<unsigned char>(packet[position + i])));
      }
    }
    position += labelLength;
  }
  return false;
}

String mdnsCameraService(const String &name) {
  String lower = name;
  lower.toLowerCase();
  if (lower.indexOf("_onvif._tcp.local") >= 0) {
    return F("ONVIF");
  }
  if (lower.indexOf("_rtsp._tcp.local") >= 0) {
    return F("RTSP");
  }
  if (lower.indexOf("_camera._tcp.local") >= 0) {
    return F("Camera");
  }
  if (lower.indexOf("_nvr._tcp.local") >= 0) {
    return F("NVR");
  }
  return "";
}

bool parseGenericMdnsResponse(const uint8_t *packet, size_t packetLength,
                              IPAddress sender) {
  if (packetLength < 12 || (packet[2] & 0x80) == 0 ||
      !isStaSubnetUnicastTarget(sender)) {
    return false;
  }
  const uint16_t questionCount = readBigEndian16(packet + 4);
  const uint32_t recordCount =
      static_cast<uint32_t>(readBigEndian16(packet + 6)) +
      readBigEndian16(packet + 8) + readBigEndian16(packet + 10);
  if (recordCount > 128) {
    return false;
  }

  size_t cursor = 12;
  for (uint16_t i = 0; i < questionCount; ++i) {
    String question;
    if (!readMdnsName(packet, packetLength, cursor, question) ||
        packetLength - cursor < 4) {
      return false;
    }
    cursor += 4;
  }

  String services;
  String endpoint;
  for (uint32_t i = 0; i < recordCount; ++i) {
    String owner;
    if (!readMdnsName(packet, packetLength, cursor, owner) ||
        packetLength - cursor < 10) {
      return false;
    }
    const uint16_t type = readBigEndian16(packet + cursor);
    const uint16_t dataLength = readBigEndian16(packet + cursor + 8);
    const size_t dataStart = cursor + 10;
    if (dataLength > packetLength - dataStart) {
      return false;
    }
    const size_t dataEnd = dataStart + dataLength;
    String service = mdnsCameraService(owner);
    if (type == 12) {
      size_t targetCursor = dataStart;
      String target;
      if (readMdnsName(packet, packetLength, targetCursor, target) &&
          targetCursor <= dataEnd && service.isEmpty()) {
        service = mdnsCameraService(target);
      }
    } else if (type == 33 && dataLength >= 7) {
      size_t targetCursor = dataStart + 6;
      String target;
      if (readMdnsName(packet, packetLength, targetCursor, target) &&
          targetCursor <= dataEnd && service.isEmpty()) {
        service = mdnsCameraService(target);
      }
      if (!service.isEmpty()) {
        endpoint = target + ':' + String(readBigEndian16(packet + dataStart + 4));
      }
    }
    if (!service.isEmpty() && services.indexOf(service) < 0) {
      if (!services.isEmpty()) {
        services += F(", ");
      }
      services += service;
    }
    cursor = dataEnd;
  }
  if (services.isEmpty()) {
    return false;
  }
  addGenericDiscoveryDevice(sender, F("mDNS/DNS-SD"), services, endpoint);
  return true;
}

bool collectGenericMdnsResponses() {
  static const char *serviceNames[] = {
      "_onvif._tcp.local", "_rtsp._tcp.local", "_camera._tcp.local",
      "_nvr._tcp.local"};
  uint8_t query[256] = {};
  query[5] = sizeof(serviceNames) / sizeof(serviceNames[0]);
  size_t queryLength = 12;
  for (const char *serviceName : serviceNames) {
    const String name(serviceName);
    size_t labelStart = 0;
    while (labelStart < name.length()) {
      const int dot = name.indexOf('.', labelStart);
      const size_t labelEnd =
          dot < 0 ? name.length() : static_cast<size_t>(dot);
      const size_t labelLength = labelEnd - labelStart;
      if (labelLength > 63 || queryLength + labelLength + 1 >= sizeof(query)) {
        return false;
      }
      query[queryLength++] = static_cast<uint8_t>(labelLength);
      for (size_t i = labelStart; i < labelEnd; ++i) {
        query[queryLength++] = static_cast<uint8_t>(name[i]);
      }
      if (dot < 0) {
        break;
      }
      labelStart = labelEnd + 1;
    }
    query[queryLength++] = 0;
    query[queryLength++] = 0;
    query[queryLength++] = 12;
    query[queryLength++] = 0;
    query[queryLength++] = 1;
  }

  WiFiUDP udp;
  if (!udp.beginMulticast(GENERIC_MDNS_MULTICAST_IP, GENERIC_MDNS_PORT)) {
    return false;
  }
  if (udp.beginPacket(GENERIC_MDNS_MULTICAST_IP, GENERIC_MDNS_PORT) != 1 ||
      udp.write(query, queryLength) != queryLength || udp.endPacket() != 1) {
    udp.stop();
    return false;
  }

  const uint32_t startedAt = millis();
  uint8_t packet[2048];
  while (millis() - startedAt < GENERIC_DISCOVERY_TIMEOUT_MS) {
    const int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(10);
      continue;
    }
    const IPAddress sender = udp.remoteIP();
    if (!isStaSubnetUnicastTarget(sender) ||
        static_cast<size_t>(packetSize) > sizeof(packet)) {
      while (udp.available()) {
        udp.read();
      }
      continue;
    }
    const int bytesRead = udp.read(packet, static_cast<size_t>(packetSize));
    while (udp.available()) {
      udp.read();
    }
    if (bytesRead > 0) {
      parseGenericMdnsResponse(packet, static_cast<size_t>(bytesRead), sender);
    }
  }
  udp.stop();
  return true;
}

void handleGenericDeviceDiscovery() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a la LAN antes de buscar dispositivos genéricos");
    return;
  }

  genericDiscoveryDevices.clear();
  if (!collectGenericOnvifResponses()) {
    sendError(503, "No se pudo enviar o escuchar la consulta ONVIF");
    return;
  }
  if (!collectGenericSsdpResponses()) {
    sendError(503, "No se pudo enviar o escuchar la consulta SSDP/UPnP");
    return;
  }
  if (!collectGenericMdnsResponses()) {
    sendError(503, "No se pudo enviar o escuchar las consultas mDNS");
    return;
  }
  Serial.printf("[GENERIC] %u servicios/dispositivos encontrados\n",
                static_cast<unsigned>(genericDiscoveryDevices.size()));
  std::vector<DiscoveryHistoryRecord> records;
  records.reserve(genericDiscoveryDevices.size());
  for (const GenericDiscoveryDevice &device : genericDiscoveryDevices) {
    DiscoveryHistoryRecord record{};
    record.source = F("GENERIC");
    record.ip = device.ip;
    record.protocol = device.protocol;
    record.identity = device.identity;
    record.endpoint = device.endpoint;
    records.push_back(record);
  }
  const DiscoveryHistorySaveStatus saveStatus =
      saveDiscoveryHistory(records, server.arg("recordDate"),
                           server.arg("recordTime"));
  sendJson(200, genericDiscoveryDevicesJson(saveStatus));
}

const RouterTableRecord *currentRouterTable() {
  if (WiFi.status() != WL_CONNECTED || gatewayMac.isEmpty()) {
    return nullptr;
  }

  const String gatewayIp = WiFi.gatewayIP().toString();
  const String network = WiFi.SSID() + "|" + WiFi.BSSIDstr();
  if (gatewayMacIp != gatewayIp || gatewayMacNetwork != network) {
    return nullptr;
  }

  String normalizedGatewayMac;
  if (!normalizeMacAddress(gatewayMac, normalizedGatewayMac)) {
    return nullptr;
  }
  for (size_t i = 0; i < routerTableRecordCount; ++i) {
    const RouterTableRecord &table = routerTableRecords[i];
    if (gatewayIp == table.gatewayIp &&
        normalizedGatewayMac == table.gatewayMac) {
      return &table;
    }
  }
  return nullptr;
}

bool isKnownLanHttpTarget(const IPAddress &target) {
  const String ip = target.toString();
  const bool observedByArp =
      std::any_of(arpObservations.begin(), arpObservations.end(),
                  [&ip](const ArpObservation &entry) { return entry.ip == ip; });
  const bool importedToInventory =
      std::any_of(cctvAssets.begin(), cctvAssets.end(),
                  [&ip](const CctvAsset &entry) { return entry.ip == ip; });
  const bool discovered =
      std::any_of(sadpDevices.begin(), sadpDevices.end(),
                  [&ip](const SadpDevice &entry) { return entry.ip == ip; }) ||
      std::any_of(dhDiscoverDevices.begin(), dhDiscoverDevices.end(),
                  [&ip](const DhDiscoverDevice &entry) { return entry.ip == ip; }) ||
      std::any_of(genericDiscoveryDevices.begin(), genericDiscoveryDevices.end(),
                  [&ip](const GenericDiscoveryDevice &entry) { return entry.ip == ip; });
  if (observedByArp || importedToInventory || discovered) {
    return true;
  }

  const RouterTableRecord *table = currentRouterTable();
  if (!table) {
    return false;
  }
  for (size_t i = table->firstBinding;
       i < table->firstBinding + table->bindingCount; ++i) {
    if (routerBindingRecords[i].enabled &&
        ip == routerBindingRecords[i].ip) {
      return true;
    }
  }
  return false;
}

void handleDevicePing() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a la LAN antes de hacer ping");
    return;
  }

  const String ipText = server.arg("ip");
  IPAddress target;
  if (!target.fromString(ipText) || target.toString() != ipText) {
    sendError(400, "La IP del dispositivo no es válida");
    return;
  }

  if (!isStaSubnetUnicastTarget(target)) {
    sendError(403, "El ping se limita a una IP unicast de la LAN conectada");
    return;
  }

  const uint32_t now = millis();
  if (!devicePingWindowStarted || now - devicePingWindowStartedAt >= 60000) {
    devicePingWindowStarted = true;
    devicePingWindowStartedAt = now;
    devicePingAttemptsInWindow = 0;
  }
  if (devicePingAttemptsInWindow >= 30) {
    sendError(429, "Se alcanzó el límite de 30 pings por minuto");
    return;
  }
  if (devicePingAttempted && now - lastDevicePingAt < 1000) {
    sendError(429, "Espera antes de enviar el siguiente ping");
    return;
  }
  devicePingAttempted = true;
  lastDevicePingAt = now;
  ++devicePingAttemptsInWindow;

  const bool reachable = Ping.ping(target, 1);
  String body = F("{\"target\":\"");
  body += target.toString();
  body += F("\",\"reachable\":");
  body += reachable ? F("true") : F("false");
  body += F(",\"averageMs\":");
  if (reachable) {
    body += String(Ping.averageTime(), 1);
  } else {
    body += F("null");
  }
  body += '}';
  sendJson(200, body);
}

bool readHttpHeaderLine(WiFiClient &client, String &line, uint32_t startedAt) {
  line = "";
  while (millis() - startedAt < 4000UL) {
    while (client.available() > 0) {
      const int value = client.read();
      if (value < 0) {
        break;
      }
      if (value == '\n') {
        return true;
      }
      if (value != '\r' && line.length() < 256) {
        line += static_cast<char>(value);
      }
    }
    if (!client.connected()) {
      return !line.isEmpty();
    }
    delay(1);
  }
  return false;
}

bool responseIsHtml(WiFiClient &client, int &statusCode) {
  const uint32_t startedAt = millis();
  String line;
  if (!readHttpHeaderLine(client, line, startedAt) || !line.startsWith("HTTP/")) {
    return false;
  }
  const int statusSeparator = line.indexOf(' ');
  if (statusSeparator < 0) {
    return false;
  }
  statusCode = line.substring(statusSeparator + 1,
                              statusSeparator + 4)
                   .toInt();
  if (statusCode < 100 || statusCode > 599) {
    return false;
  }

  String contentType;
  size_t headerBytes = line.length();
  bool headersComplete = false;
  while (headerBytes < 2048 && readHttpHeaderLine(client, line, startedAt)) {
    headerBytes += line.length() + 2;
    if (line.isEmpty()) {
      headersComplete = true;
      break;
    }
    const int separator = line.indexOf(':');
    if (separator > 0 &&
        line.substring(0, separator).equalsIgnoreCase("Content-Type")) {
      contentType = line.substring(separator + 1);
      contentType.trim();
      contentType.toLowerCase();
    }
  }
  if (contentType.startsWith("text/html")) {
    return true;
  }
  if (!headersComplete) {
    return false;
  }

  String bodyPrefix;
  bodyPrefix.reserve(512);
  const uint32_t bodyStartedAt = millis();
  while (millis() - bodyStartedAt < 500UL && bodyPrefix.length() < 512) {
    while (client.available() > 0 && bodyPrefix.length() < 512) {
      const int value = client.read();
      if (value >= 0) {
        bodyPrefix += static_cast<char>(value);
      }
    }
    if (!client.connected()) {
      break;
    }
    delay(1);
  }
  bodyPrefix.toLowerCase();
  return bodyPrefix.indexOf("<html") >= 0 ||
         bodyPrefix.indexOf("<!doctype html") >= 0 ||
         bodyPrefix.indexOf("<head") >= 0 ||
         bodyPrefix.indexOf("<body") >= 0;
}

void handleHttpServerCheck() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a la LAN antes de comprobar HTTP");
    return;
  }

  const String ipText = server.arg("ip");
  IPAddress target;
  if (!target.fromString(ipText) || target.toString() != ipText) {
    sendError(400, "La IP del dispositivo no es válida");
    return;
  }
  if (!isStaSubnetUnicastTarget(target)) {
    sendError(403,
              "La comprobación HTTP se limita a una IP unicast de la LAN "
              "conectada");
    return;
  }
  if (!isKnownLanHttpTarget(target)) {
    sendError(403,
              "La comprobación HTTP solo admite IP observadas por ARP o "
              "registradas en el inventario");
    return;
  }

  const String portText = server.arg("port");
  int port = 80;
  if (!portText.isEmpty()) {
    if (portText != "80" && portText != "8080") {
      sendError(400, "Solo se permite comprobar HTTP en los puertos 80 y 8080");
      return;
    }
    port = portText.toInt();
  }

  const uint32_t now = millis();
  if (!httpProbeWindowStarted || now - httpProbeWindowStartedAt >= 60000UL) {
    httpProbeWindowStarted = true;
    httpProbeWindowStartedAt = now;
    httpProbeAttemptsInWindow = 0;
  }
  if (httpProbeAttemptsInWindow >= 64) {
    sendError(429, "Se alcanzó el límite de comprobaciones HTTP por minuto");
    return;
  }
  if (httpProbeAttempted && now - lastHttpProbeAt < 1000UL) {
    sendError(429, "Espera antes de comprobar otra IP");
    return;
  }
  httpProbeAttempted = true;
  lastHttpProbeAt = now;
  ++httpProbeAttemptsInWindow;

  WiFiClient client;
  client.setTimeout(3000);
  const uint32_t connectStartedAt = millis();
  bool connected = client.connect(target, port, 2500);
  if (!connected) {
    client.stop();
    delay(200);
    connected = client.connect(target, port, 2500);
  }
  const uint32_t latencyMs = millis() - connectStartedAt;
  int statusCode = 0;
  bool html = false;
  if (connected) {
    client.print(F("GET / HTTP/1.1\r\nHost: "));
    client.print(ipText);
    if (port != 80) {
      client.print(':');
      client.print(port);
    }
    client.print(F("\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n"));
    html = responseIsHtml(client, statusCode);
  }
  client.stop();

  String body = F("{\"ip\":\"");
  body += target.toString();
  body += F("\",\"port\":");
  body += String(port);
  body += F(",\"connected\":");
  body += connected ? F("true") : F("false");
  body += F(",\"valid\":");
  body += statusCode >= 100 && statusCode <= 599 ? F("true") : F("false");
  body += F(",\"html\":");
  body += html ? F("true") : F("false");
  body += F(",\"status\":");
  body += String(statusCode);
  body += F(",\"latencyMs\":");
  body += String(latencyMs);
  body += '}';
  sendJson(200, body);
}

void handleRouterBindings() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA para seleccionar la tabla del gateway");
    return;
  }

  const String gatewayIp = WiFi.gatewayIP().toString();
  const String network = WiFi.SSID() + "|" + WiFi.BSSIDstr();
  const String activeGatewayMac =
      gatewayMacIp == gatewayIp && gatewayMacNetwork == network ? gatewayMac
                                                               : String();
  const RouterTableRecord *table = currentRouterTable();

  String body;
  body.reserve(256 + (table ? table->bindingCount * 190 : 0));
  body = F("{\"matched\":");
  body += table ? F("true") : F("false");
  body += F(",\"gatewayIp\":\"");
  body += jsonEscape(gatewayIp);
  body += F("\",\"gatewayMac\":\"");
  body += jsonEscape(activeGatewayMac);
  body += F("\",\"brand\":\"");
  if (table) {
    body += jsonEscape(String(table->brand));
  }
  body += F("\",\"reason\":\"");
  if (!table) {
    body += activeGatewayMac.isEmpty() ? F("gateway_mac_pending")
                                       : F("no_matching_table");
    body += F("\",\"bindings\":[]}");
    sendJson(200, body);
    return;
  }
  body += F("\",\"subnet\":\"");
  body += jsonEscape(String(table->subnet));
  body += F("\",\"bindings\":[");

  bool first = true;
  for (size_t i = table->firstBinding;
       i < table->firstBinding + table->bindingCount; ++i) {
    const RouterBindingRecord &binding = routerBindingRecords[i];
    if (!binding.enabled) {
      continue;
    }

    const auto observed = std::find_if(
        arpObservations.begin(), arpObservations.end(),
        [&binding](const ArpObservation &entry) {
          return entry.ip == binding.ip && entry.mac == binding.mac;
        });
    if (!first) {
      body += ',';
    }
    first = false;
    body += F("{\"ip\":\"");
    body += jsonEscape(String(binding.ip));
    body += F("\",\"mac\":\"");
    body += jsonEscape(String(binding.mac));
    body += F("\",\"description\":\"");
    body += jsonEscape(String(binding.description));
    body += F("\",\"arpObserved\":");
    body += observed != arpObservations.end() ? F("true") : F("false");
    body += F(",\"lastSeenSeconds\":");
    body += observed != arpObservations.end()
                ? String((millis() - observed->lastSeenAt) / 1000UL)
                : String("null");
    body += '}';
  }
  body += F("]}");
  sendJson(200, body);
}

void handleTransferTest() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a Internet antes de medir");
    return;
  }

  const long requestedBytes = server.arg("bytes").toInt();
  if (requestedBytes < 0) {
    sendError(400, "El tamaño de descarga no puede ser negativo");
    return;
  }
  size_t bytes = requestedBytes == 0
                     ? 3U * 1024U * 1024U
                     : static_cast<size_t>(requestedBytes);
  bytes = std::max(MIN_TEST_BYTES, std::min(bytes, MAX_TEST_BYTES));

  NetworkClientSecure tlsClient;
  tlsClient.setTimeout(TEST_DOWNLOAD_IDLE_TIMEOUT_MS);
  tlsClient.setHandshakeTimeout(15);
  tlsClient.useBuiltinCACertBundle();

  HTTPClient http;
  http.setConnectTimeout(TEST_DOWNLOAD_IDLE_TIMEOUT_MS);
  http.setTimeout(TEST_DOWNLOAD_IDLE_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setAcceptEncoding("identity");
  const String url =
      String(F("https://speed.cloudflare.com/__down?bytes=")) +
      String(bytes) + F("&cacheBust=") + String(millis());
  if (!http.begin(tlsClient, url)) {
    const String detail = "No se pudo iniciar la conexión con speed.cloudflare.com";
    recordError("Test Internet", detail);
    sendError(502, detail);
    return;
  }

  const uint32_t requestStartedAt = millis();
  const int status = http.GET();
  const uint32_t latencyMs = millis() - requestStartedAt;
  if (status != HTTP_CODE_OK) {
    const String detail = "speed.cloudflare.com: HTTP " + String(status) +
                          " (" + http.errorToString(status) + ")";
    http.end();
    tlsClient.stop();
    recordError("Test Internet", detail);
    sendError(502, detail);
    return;
  }

  NetworkClient *stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    tlsClient.stop();
    const String detail = "No se recibió el flujo de descarga externo";
    recordError("Test Internet", detail);
    sendError(502, detail);
    return;
  }

  uint8_t buffer[1024];
  size_t received = 0;
  const uint32_t transferStartedAt = millis();
  uint32_t lastDataAt = transferStartedAt;
  bool timedOut = false;
  while (received < bytes) {
    const int available = stream->available();
    if (available > 0) {
      const size_t chunk =
          std::min(static_cast<size_t>(available), sizeof(buffer));
      const size_t count =
          stream->readBytes(reinterpret_cast<char *>(buffer), chunk);
      if (count == 0) {
        break;
      }
      received += count;
      lastDataAt = millis();
    } else {
      const uint32_t now = millis();
      if (now - transferStartedAt >= TEST_DOWNLOAD_TOTAL_TIMEOUT_MS ||
          now - lastDataAt >= TEST_DOWNLOAD_IDLE_TIMEOUT_MS) {
        timedOut = true;
        break;
      }
      if (!stream->connected()) {
        break;
      }
      delay(1);
    }
    if (millis() - transferStartedAt >= TEST_DOWNLOAD_TOTAL_TIMEOUT_MS) {
      timedOut = true;
      break;
    }
  }
  const uint32_t transferMs =
      std::max<uint32_t>(1, millis() - transferStartedAt);
  http.end();
  tlsClient.stop();

  if (received != bytes) {
    const String detail =
        String(timedOut ? "Tiempo agotado al descargar desde speed.cloudflare.com: "
                        : "Descarga externa incompleta: ") +
        String(static_cast<unsigned long>(received)) + " de " +
        String(static_cast<unsigned long>(bytes)) + " bytes";
    recordError("Test Internet", detail);
    sendError(timedOut ? 504 : 502, detail);
    return;
  }

  const float downloadMbps =
      static_cast<float>(received) * 8.0f /
      (static_cast<float>(transferMs) / 1000.0f) / 1000000.0f;
  String body;
  body.reserve(192);
  body = F("{\"ok\":true,\"server\":\"speed.cloudflare.com\",\"bytesReceived\":");
  body += String(static_cast<unsigned long>(received));
  body += F(",\"downloadMbps\":");
  body += String(downloadMbps, 3);
  body += F(",\"latencyMs\":");
  body += String(latencyMs);
  body += F(",\"transferMs\":");
  body += String(transferMs);
  body += '}';
  sendJson(200, body);
}

void handleRecordPoint() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }

  const String name = sanitizePointName(server.arg("name"));
  float mbps = 0;
  float latencyMs = 0;
  if (name.isEmpty() || !parseFloatArgument(server.arg("mbps"), mbps) ||
      !parseFloatArgument(server.arg("latencyMs"), latencyMs) || mbps < 0 ||
      mbps > 10000 || latencyMs < 0 || latencyMs > 120000) {
    sendError(400, "Nombre, velocidad o latencia no válidos");
    return;
  }

  SurveyPoint point{millis(), name, mbps, WiFi.RSSI(),
                    static_cast<uint8_t>(WiFi.channel()), latencyMs};
  SurveyPoint previous;
  const bool hasPrevious = findPreviousPoint(name, previous);
  const PointAnalysis analysis =
      analyzePoint(point, hasPrevious ? &previous : nullptr);
  if (!appendSurveyPoint(point)) {
    sendError(500, "No se pudo guardar el punto en LittleFS");
    return;
  }
  latestAnalysis = analysis;
  latestPointName = name;

  if (point.rssi < 0) {
    Serial.printf("Punto %s: %.3f Mbps, RSSI %ld dBm, canal %u\n",
                  point.name.c_str(), point.mbps,
                  static_cast<long>(point.rssi), point.channel);
  } else {
    Serial.printf("Punto %s: %.3f Mbps, RSSI no disponible, canal %u\n",
                  point.name.c_str(), point.mbps, point.channel);
  }
  sendJson(201, "{\"ok\":true}");
}

void handlePoints() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }
  String points;
  if (!pointsJson(points)) {
    sendError(500, "No se pudo leer la lista de puntos en LittleFS");
    return;
  }
  sendJson(200, points);
}

void handleClearPoints() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }
  if (LittleFS.exists(POINTS_FILE) && !LittleFS.remove(POINTS_FILE)) {
    Serial.println(F("ERROR: no se pudo borrar points.csv"));
    sendError(500, "No se pudieron borrar los puntos guardados");
    return;
  }

  latestPointName = "";
  latestAnalysis =
      {0, "Sin datos", "Ejecuta una medición para analizar el punto", ""};
  updateDisplay();
  Serial.println(F("[Portal] Se borraron los puntos guardados"));
  sendJson(200, "{\"ok\":true}");
}

void handleCctvAssets() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }
  sendJson(200, cctvAssetsJson());
}

String cctvHistoryJson(const std::vector<CctvHistoryRecord> &records) {
  String body;
  body.reserve(128 + records.size() * 240);
  body = '[';
  for (size_t i = 0; i < records.size(); ++i) {
    if (i != 0) {
      body += ',';
    }
    const CctvHistoryRecord &record = records[i];
    body += F("{\"date\":\"");
    body += jsonEscape(record.date);
    body += F("\",\"time\":\"");
    body += jsonEscape(record.time);
    body += F("\",\"ip\":\"");
    body += jsonEscape(record.asset.ip);
    body += F("\",\"name\":\"");
    body += jsonEscape(record.asset.name);
    body += F("\",\"type\":\"");
    body += jsonEscape(record.asset.type);
    body += F("\",\"mac\":\"");
    body += jsonEscape(record.asset.mac);
    body += F("\",\"manufacturer\":\"");
    body += jsonEscape(record.asset.manufacturer);
    body += F("\",\"model\":\"");
    body += jsonEscape(record.asset.model);
    body += F("\",\"firmware\":\"");
    body += jsonEscape(record.asset.firmware);
    body += F("\",\"ipMode\":\"");
    body += jsonEscape(record.asset.ipMode);
    body += F("\",\"rssi\":");
    body += record.hasRssi ? String(record.rssi) : String(F("null"));
    body += '}';
  }
  body += ']';
  return body;
}

void handleCctvHistory() {
  if (!requireSession()) {
    return;
  }
  if (!sdCardReady || sdFormatRunning) {
    sendError(503, "La microSD no está disponible para leer el historial CCTV");
    return;
  }
  std::vector<CctvHistoryRecord> records;
  if (!loadCctvHistory(records, 300)) {
    sendError(500, "No se pudo leer el historial CCTV de la microSD");
    return;
  }
  sendJson(200, cctvHistoryJson(records));
}

void handleDiscoveryHistory() {
  if (!requireSession()) {
    return;
  }
  if (!sdCardReady || sdFormatRunning) {
    sendError(503,
              "La microSD no está disponible para leer el historial de descubrimientos");
    return;
  }
  std::vector<DiscoveryHistoryRecord> records;
  bool truncated = false;
  if (!loadDiscoveryHistory(records, truncated)) {
    sendError(500,
              "No se pudo leer el historial de descubrimientos de la microSD");
    return;
  }
  sendJson(200, discoveryHistoryJson(records, truncated));
}

void handleCctvHistoryCsvDownload() {
  if (!requireSession()) {
    return;
  }
  if (!sdCardReady || sdFormatRunning) {
    sendError(503, "La microSD no está disponible para descargar el historial");
    return;
  }
  const String date = server.arg("date");
  if (!isValidCctvHistoryDate(date)) {
    sendError(400, "La fecha debe tener formato válido AAAA-MM-DD");
    return;
  }
  std::vector<CctvHistoryRecord> records;
  if (!loadCctvHistory(records, 2000, &date)) {
    sendError(500, "No se pudo leer el historial CCTV de la microSD");
    return;
  }

  String body =
      F("date,time,ip,name,type,mac,manufacturer,model,firmware,ip_mode,"
        "wifi_rssi_dbm\r\n");
  body.reserve(body.length() + records.size() * 256);
  for (const CctvHistoryRecord &record : records) {
    if (record.date == date) {
      body += cctvHistoryCsvLine(record);
    }
  }
  server.sendHeader("Content-Disposition",
                    "attachment; filename=cctv_inventory_" + date + ".csv");
  server.send(200, "text/csv; charset=utf-8", body);
}

void handleArpNeighbors() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a la red para observar vecinos ARP");
    return;
  }
  sendJson(200, arpNeighborsJson());
}

void handleScanCsvDownload() {
  if (!requireSession()) {
    return;
  }

  String body = F("ssid,bssid,channel,rssi_dbm,encryption,evaluation\r\n");
  body.reserve(body.length() + scanResults.size() * 128);
  for (const ScannedNetwork &network : scanResults) {
    body += csvEscape(network.ssid);
    body += ',';
    body += csvEscape(network.bssid);
    body += ',';
    body += String(network.channel);
    body += ',';
    body += String(network.rssi);
    body += ',';
    body += csvEscape(encryptionLabel(network.auth));
    body += ',';
    body += csvEscape(riskLabel(network.auth));
    body += F("\r\n");
  }

  server.sendHeader("Content-Disposition",
                    "attachment; filename=wifi_scan.csv");
  server.send(200, "text/csv; charset=utf-8", body);
}

void handleArpNeighborsCsvDownload() {
  if (!requireSession()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendError(503, "Conecta la STA a la red para exportar vecinos ARP");
    return;
  }

  String body = F("ip,mac,last_seen_seconds\r\n");
  body.reserve(body.length() + arpObservations.size() * 120);
  for (const ArpObservation &entry : arpObservations) {
    body += csvEscape(entry.ip);
    body += ',';
    body += csvEscape(entry.mac);
    body += ',';
    body += String((millis() - entry.lastSeenAt) / 1000UL);
    body += F("\r\n");
  }

  server.sendHeader("Content-Disposition",
                    "attachment; filename=arp_observations.csv");
  server.send(200, "text/csv; charset=utf-8", body);
}

void handleAddCctvAsset() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }
  if (!sdCardReady || sdFormatRunning) {
    sendError(503, "La microSD debe estar montada para registrar el historial CCTV");
    return;
  }

  IPAddress ip;
  String mac;
  const String recordDate = server.arg("recordDate");
  const String recordTime = server.arg("recordTime");
  const String ipInput = server.arg("ip");
  const String name = sanitizeAssetValue(server.arg("name"), 32);
  const String type = server.arg("type");
  const String ipMode = server.arg("ipMode");
  const String manufacturer =
      sanitizeAssetValue(server.arg("manufacturer"), 32);
  const String model = sanitizeAssetValue(server.arg("model"), 32);
  const String firmware = sanitizeAssetValue(server.arg("firmware"), 24);
  const String macInput = server.arg("mac");
  const bool hasIp = ipMode == "manual" || ipMode == "detected";
  if (!isValidCctvHistoryDate(recordDate) ||
      !isValidCctvHistoryTime(recordTime) || !isCctvIpMode(ipMode) ||
      name.isEmpty() || !isCctvAssetType(type) || manufacturer.isEmpty() ||
      model.isEmpty() ||
      (ipMode == "none" && !ipInput.isEmpty()) ||
      (hasIp && !ip.fromString(ipInput)) ||
      (!macInput.isEmpty() && !normalizeMacAddress(macInput, mac))) {
    sendError(400, "Revisa fecha/hora local, nombre, tipo, fabricante, modelo, "
                   "IP y MAC si se ingresaron");
    return;
  }

  CctvAsset asset{hasIp ? ip.toString() : String(""),
                  name,
                  type,
                  mac,
                  manufacturer,
                  model,
                  firmware,
                  ipMode};
  for (const CctvAsset &saved : cctvAssets) {
    const bool duplicateDetectedIp =
        asset.ipMode == "detected" && saved.ipMode != "manual" &&
        saved.ipMode != "none" && saved.ip == asset.ip;
    const bool duplicateMac =
        !asset.mac.isEmpty() && saved.mac == asset.mac;
    if (duplicateDetectedIp || duplicateMac) {
      sendError(409, "La IP o la MAC ya están registradas");
      return;
    }
  }
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const CctvHistoryRecord historyRecord{
      recordDate, recordTime, asset, wifiConnected,
      wifiConnected ? static_cast<int32_t>(WiFi.RSSI()) : 0};
  if (!appendCctvHistoryRecord(historyRecord)) {
    recordError("microSD", "No se pudo guardar el registro CCTV en microSD");
    sendError(500, "No se pudo guardar el registro diario CCTV en la microSD");
    return;
  }
  if (!appendCctvAsset(asset)) {
    sendError(500, "El registro quedó en microSD, pero no se pudo guardar el "
                   "equipo en LittleFS");
    return;
  }
  cctvAssets.push_back(asset);
  Serial.printf("[Inventario] Equipo agregado: %s (%s)\n", asset.name.c_str(),
                asset.ip.isEmpty() ? "sin IP" : asset.ip.c_str());
  String response = F("{\"ok\":true,\"rssi\":");
  response += wifiConnected ? String(WiFi.RSSI()) : String(F("null"));
  response += '}';
  sendJson(201, response);
}

void handleClearCctvAssets() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }
  if (LittleFS.exists(CCTV_ASSETS_FILE) &&
      !LittleFS.remove(CCTV_ASSETS_FILE)) {
    Serial.println(F("ERROR: no se pudo borrar el inventario CCTV"));
    sendError(500, "No se pudo borrar el inventario CCTV");
    return;
  }
  cctvAssets.clear();
  Serial.println(F("[Inventario] Se borró el inventario CCTV"));
  sendJson(200, "{\"ok\":true}");
}

void handleClientError() {
  if (!requireSession()) {
    return;
  }

  String message = server.arg("message");
  message.trim();
  if (message.isEmpty()) {
    sendError(400, "El mensaje de error está vacío");
    return;
  }
  if (message.length() > 240) {
    message.remove(240);
  }
  for (size_t i = 0; i < message.length(); ++i) {
    if (static_cast<uint8_t>(message[i]) < 0x20 ||
        static_cast<uint8_t>(message[i]) == 0x7f) {
      message[i] = ' ';
    }
  }
  Serial.printf("[Portal] Error del navegador: %s\n", message.c_str());
  recordError("Navegador", message);
  server.send(204, "text/plain", "");
}

void handleErrorLogDownload() {
  if (!requireSession()) {
    return;
  }

  String content;
  if (!readErrorLog(content, false)) {
    sendError(503, "No se pudo leer el log de errores en LittleFS");
    return;
  }
  if (content.isEmpty()) {
    content = F("Sin errores registrados.\r\n");
  }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition",
                    "attachment; filename=esp32_error_log.txt");
  server.send(200, "text/plain; charset=utf-8", content);
}

void handleCsvDownload() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }
  if (!LittleFS.exists(POINTS_FILE)) {
    File emptyFile = LittleFS.open(POINTS_FILE, FILE_WRITE);
    if (!emptyFile) {
      sendError(500, "No se pudo crear el archivo CSV");
      return;
    }
    emptyFile.println(F("uptime_ms,name,mbps,rssi_dbm,channel,latency_ms"));
    emptyFile.close();
  }
  File file = LittleFS.open(POINTS_FILE, FILE_READ);
  if (!file) {
    sendError(500, "No se pudo abrir el archivo CSV");
    return;
  }
  server.sendHeader("Content-Disposition",
                    "attachment; filename=site_survey_points.csv");
  server.streamFile(file, "text/csv; charset=utf-8");
  file.close();
}

void handleCctvAssetsCsvDownload() {
  if (!requireSession()) {
    return;
  }
  if (!filesystemReady) {
    sendError(503, "LittleFS no está disponible");
    return;
  }
  if (!loadCctvAssets()) {
    sendError(500, "No se pudo leer el inventario CCTV");
    return;
  }

  server.sendHeader("Content-Disposition",
                    "attachment; filename=cctv_inventory.csv");
  String body = F("ip,name,type,mac,manufacturer,model,firmware,ip_mode\r\n");
  body.reserve(body.length() + cctvAssets.size() * 200);
  for (const CctvAsset &asset : cctvAssets) {
    body += csvEscape(asset.ip);
    body += ',';
    body += csvEscape(asset.name);
    body += ',';
    body += csvEscape(asset.type);
    body += ',';
    body += csvEscape(asset.mac);
    body += ',';
    body += csvEscape(asset.manufacturer);
    body += ',';
    body += csvEscape(asset.model);
    body += ',';
    body += csvEscape(asset.firmware);
    body += ',';
    body += csvEscape(asset.ipMode);
    body += F("\r\n");
  }
  server.send(200, "text/csv; charset=utf-8", body);
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    sendError(404, "Ruta API no encontrada");
    return;
  }
  server.sendHeader("Location", "http://192.168.4.1/");
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain", "");
}

void handleRoot();
void handleDeviceAlias();
void handleOtaStatus();
void handleOtaConfig();
void handleOtaCheck();
void handleOtaUpdate();

void configureRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, []() {
    if (requireSession()) {
      sendJson(200, statusJson());
    }
  });
  server.on("/api/ota/status", HTTP_GET, handleOtaStatus);
  server.on("/api/ota/config", HTTP_POST, handleOtaConfig);
  server.on("/api/ota/check", HTTP_POST, handleOtaCheck);
  server.on("/api/ota/update", HTTP_POST, handleOtaUpdate);
  server.on("/api/device/alias", HTTP_POST, handleDeviceAlias);
  server.on("/api/scan", HTTP_POST, handleScan);
  server.on("/api/connect", HTTP_POST, handleConnect);
  server.on("/api/sd/format", HTTP_POST, handleSdFormat);
  server.on("/api/ping", HTTP_GET, handlePing);
  server.on("/api/gateway-ping", HTTP_GET, handleGatewayPing);
  server.on("/api/sadp/discover", HTTP_POST, handleSadpDiscovery);
  server.on("/api/dhdiscover/discover", HTTP_POST, handleDhDiscover);
  server.on("/api/generic-discovery", HTTP_POST,
            handleGenericDeviceDiscovery);
  server.on("/api/discovery-history", HTTP_GET, handleDiscoveryHistory);
  server.on("/api/device-ping", HTTP_GET, handleDevicePing);
  server.on("/api/http-server-check", HTTP_GET, handleHttpServerCheck);
  server.on("/api/router-bindings", HTTP_GET, handleRouterBindings);
  server.on("/api/test", HTTP_GET, handleTransferTest);
  server.on("/api/record", HTTP_POST, handleRecordPoint);
  server.on("/api/points", HTTP_GET, handlePoints);
  server.on("/api/points", HTTP_DELETE, handleClearPoints);
  server.on("/api/cctv-assets", HTTP_GET, handleCctvAssets);
  server.on("/api/cctv-assets", HTTP_POST, handleAddCctvAsset);
  server.on("/api/cctv-assets", HTTP_DELETE, handleClearCctvAssets);
  server.on("/api/cctv-history", HTTP_GET, handleCctvHistory);
  server.on("/api/arp-neighbors", HTTP_GET, handleArpNeighbors);
  server.on("/api/scan.csv", HTTP_GET, handleScanCsvDownload);
  server.on("/api/arp-neighbors.csv", HTTP_GET,
            handleArpNeighborsCsvDownload);
  server.on("/api/client-error", HTTP_POST, handleClientError);
  server.on("/api/error-log", HTTP_GET, handleErrorLogDownload);
  server.on("/api/points.csv", HTTP_GET, handleCsvDownload);
  server.on("/api/cctv-assets.csv", HTTP_GET, handleCctvAssetsCsvDownload);
  server.on("/api/cctv-history.csv", HTTP_GET,
            handleCctvHistoryCsvDownload);
  server.onNotFound(handleNotFound);
  server.begin();
}

void startAccessPoint() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  if (!WiFi.softAPConfig(apAddress, apAddress, apMask)) {
    Serial.println(F("ERROR: no se pudo configurar la IP del punto de acceso"));
    recordError("Wi-Fi AP", "No se pudo configurar la IP del punto de acceso");
  }
  accessPointReady = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, false, 4);
  if (accessPointReady) {
    Serial.printf("AP %s activo en %s\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str());
    dnsServerReady = dnsServer.start(53, "*", apAddress);
    if (!dnsServerReady) {
      Serial.println(F("ERROR: no se pudo iniciar DNS para el portal cautivo"));
      recordError("DNS", "No se pudo iniciar DNS para el portal cautivo");
    }
  } else {
    connectionMessage = "Error al iniciar el punto de acceso";
    Serial.println(F("ERROR: no se pudo iniciar el AP"));
    recordError("Wi-Fi AP", "No se pudo iniciar el punto de acceso");
  }
}

void provisioningEventHandler(arduino_event_t *event) {
  portENTER_CRITICAL(&bleEventMux);
  switch (event->event_id) {
  case ARDUINO_EVENT_PROV_START:
    bleStartPending = true;
    break;
  case ARDUINO_EVENT_PROV_CRED_RECV:
    memcpy(bleReceivedSsid, event->event_info.prov_cred_recv.ssid, 32);
    bleReceivedSsid[32] = '\0';
    memcpy(bleReceivedPassword, event->event_info.prov_cred_recv.password, 64);
    bleReceivedPassword[64] = '\0';
    bleCredentialsPending = true;
    break;
  case ARDUINO_EVENT_PROV_CRED_SUCCESS:
    bleSuccessPending = true;
    break;
  case ARDUINO_EVENT_PROV_CRED_FAIL:
    bleFailurePending = true;
    bleAuthFailurePending =
        event->event_info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR;
    break;
  default:
    break;
  }
  portEXIT_CRITICAL(&bleEventMux);
}

void restoreAccessPoint() {
  if ((WiFi.getMode() & WIFI_AP) != 0) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(apAddress, apAddress, apMask)) {
    Serial.println(F("ERROR: no se pudo restaurar la IP del punto de acceso"));
    recordError("Wi-Fi AP", "No se pudo restaurar la IP del punto de acceso");
  }
  accessPointReady = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, false, 4);
  if (!accessPointReady) {
    Serial.println(F("ERROR: no se pudo restaurar el punto de acceso"));
    recordError("Wi-Fi AP", "No se pudo restaurar el punto de acceso");
    return;
  }
  dnsServer.stop();
  dnsServerReady = dnsServer.start(53, "*", apAddress);
  if (!dnsServerReady) {
    Serial.println(F("ERROR: no se pudo restaurar DNS del portal cautivo"));
    recordError("DNS", "No se pudo restaurar DNS del portal cautivo");
  }
}

void startBleProvisioning(bool force) {
  if (bleProvisioningRequested || (!force && !savedNetworks.empty())) {
    return;
  }

  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    lastNetworkError = "No se pudo leer la MAC para iniciar BLE";
    Serial.println(F("ERROR: no se pudo leer la MAC para iniciar BLE"));
    recordError("BLE", lastNetworkError);
    return;
  }
  snprintf(bleServiceName, sizeof(bleServiceName), "Prov_%02X%02X%02X",
           mac[3], mac[4], mac[5]);
  snprintf(bleProofOfPossession, sizeof(bleProofOfPossession), "%02X%02X%02X%02X",
           mac[2], mac[3], mac[4], mac[5]);

  WiFi.onEvent(provisioningEventHandler);
  bleProvisioningRequested = true;
  WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BLE,
      NETWORK_PROV_SECURITY_1, bleProofOfPossession, bleServiceName, nullptr,
      nullptr, force || savedNetworks.empty());
  bleProvisioningActive = true;
  restoreAccessPoint();
  Serial.printf("Aprovisionamiento BLE iniciado: %s\n", bleServiceName);
}

void processBleProvisioningEvents() {
  bool started;
  bool succeeded;
  bool failed;
  bool authFailed;
  bool credentialsReceived;
  char ssid[sizeof(bleReceivedSsid)];
  char password[sizeof(bleReceivedPassword)];
  portENTER_CRITICAL(&bleEventMux);
  started = bleStartPending;
  succeeded = bleSuccessPending;
  failed = bleFailurePending;
  authFailed = bleAuthFailurePending;
  credentialsReceived = bleCredentialsPending;
  memcpy(ssid, bleReceivedSsid, sizeof(ssid));
  memcpy(password, bleReceivedPassword, sizeof(password));
  bleStartPending = false;
  bleSuccessPending = false;
  bleFailurePending = false;
  bleAuthFailurePending = false;
  if (succeeded || failed) {
    bleCredentialsPending = false;
  }
  portEXIT_CRITICAL(&bleEventMux);

  static bool credentialsAnnounced = false;
  if (started) {
    bleProvisioningActive = true;
    connectionMessage = "Bluetooth listo para configurar Wi-Fi";
  }
  if (credentialsReceived && !succeeded && !failed && !credentialsAnnounced) {
    credentialsAnnounced = true;
    lastNetworkError = "";
    connectionMessage = "Credenciales recibidas; probando Wi-Fi";
  }
  if (succeeded || failed) {
    credentialsAnnounced = false;
  }
  if (failed) {
    if (authFailed) {
      lastNetworkError = "BLE: clave Wi-Fi incorrecta (autenticación fallida)";
      connectionMessage = "Clave Wi-Fi incorrecta";
      recordError("BLE", lastNetworkError);
    } else {
      lastNetworkError =
          "BLE: no se encontró el AP; usa una red 2.4 GHz visible";
      connectionMessage = "Red Wi-Fi no encontrada";
      recordError("BLE", lastNetworkError);
    }
    bleProvisioningActive = true;
  }
  if (!succeeded) {
    return;
  }

  WiFiProv.endProvision();
  bleProvisioningActive = false;
  bleProvisioningRequested = false;
  restoreAccessPoint();
  if (!credentialsReceived || ssid[0] == '\0') {
    lastNetworkError = "BLE conectó, pero no se recibieron credenciales";
    recordError("BLE", lastNetworkError);
    return;
  }

  const SavedNetwork network{String(ssid), String(password)};
  if (!rememberNetwork(network)) {
    lastNetworkError = "Wi-Fi conectado, pero no se pudo guardar en NVS";
    recordError("Wi-Fi", lastNetworkError);
  } else {
    lastNetworkError = "";
  }
  connectionMessage = "Wi-Fi configurado por Bluetooth";
  if (WiFi.status() != WL_CONNECTED || WiFi.SSID() != network.ssid) {
    startStaConnection(network, false);
  }
}

void startFilesystem() {
  filesystemReady = LittleFS.begin(false);
  if (!filesystemReady) {
    Serial.println(F("ERROR: no se pudo montar LittleFS; no se formateó"));
    recordError("LittleFS", "No se pudo montar LittleFS; no se formateó");
    return;
  }
  flushPendingErrorLog();
  Serial.printf("LittleFS listo: %u bytes usados de %u\n",
                static_cast<unsigned>(LittleFS.usedBytes()),
                static_cast<unsigned>(LittleFS.totalBytes()));
}

void startSdCard() {
  if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) {
    Serial.println(F("ERROR: no se pudieron configurar los pines SDMMC"));
    sdLastMessage = "No se pudieron configurar pines SDMMC";
    recordError("microSD", sdLastMessage);
    return;
  }
  sdCardReady =
      SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_PROBING);
  if (!sdCardReady || SD_MMC.cardType() == CARD_NONE) {
    sdCardReady = false;
    sdLastMessage = "No responde; revisa inserción y lector";
    Serial.println(F("ERROR: no se montó la microSD"));
    recordError("microSD", sdLastMessage);
    return;
  }
  sdLastMessage = "MicroSD montada";
  Serial.printf("microSD lista: %llu MB totales, %llu MB usados\n",
                static_cast<unsigned long long>(SD_MMC.totalBytes() / 1048576ULL),
                static_cast<unsigned long long>(SD_MMC.usedBytes() / 1048576ULL));
}

void startPreferences() {
  preferencesReady = preferences.begin("sitewifi", false);
  if (!preferencesReady) {
    Serial.println(F("ERROR: no se pudo abrir NVS; lista blanca no persistirá"));
    recordError("NVS", "No se pudo abrir NVS; lista blanca no persistirá");
    return;
  }
  loadSavedNetworks();
  deviceAlias = preferences.getString("devalias", "");
  const uint32_t previousBootCount = preferences.getULong("bootcount", 0);
  if (previousBootCount == UINT32_MAX) {
    deviceBootCount = previousBootCount;
    recordError("NVS", "El contador persistente de arranques llegó al máximo");
  } else {
    deviceBootCount = previousBootCount + 1;
    if (preferences.putULong("bootcount", deviceBootCount) == 0) {
      recordError("NVS", "No se pudo guardar el contador de arranques");
    }
  }
  gatewayMac = preferences.getString("gwmac", "");
  gatewayMacIp = preferences.getString("gwip", "");
  gatewayMacNetwork = preferences.getString("gwnet", "");
  otaManifestUrl = preferences.getString("otamanifest", "");
  if (!otaManifestUrl.isEmpty()) {
    otaLastStatus = "Manifiesto OTA configurado; esperando conexión";
  }
  Serial.printf("Redes guardadas en lista blanca: %u\n",
                static_cast<unsigned>(savedNetworks.size()));
}

bool isValidOtaManifestUrl(const String &url) {
  constexpr char prefix[] = "https://raw.githubusercontent.com/";
  if (!url.startsWith(prefix) || url.length() > 255) {
    return false;
  }
  for (size_t i = 0; i < url.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(url[i]);
    if (c <= 0x20 || c == '"' || c == '\\' || c == '#') {
      return false;
    }
  }
  return url.indexOf('/', sizeof(prefix) - 1) >= 0;
}

bool isValidDeviceAlias(const String &alias) {
  if (!alias.startsWith("CCTV-") || alias.length() < 9 ||
      alias.length() > 13) {
    return false;
  }
  for (size_t i = 5; i < alias.length(); ++i) {
    if (!isdigit(static_cast<unsigned char>(alias[i]))) {
      return false;
    }
  }
  return true;
}

void handleDeviceAlias() {
  if (!requireSession()) {
    return;
  }
  if (!preferencesReady) {
    sendError(500, "No se puede guardar el alias en NVS");
    return;
  }
  String alias = server.arg("alias");
  alias.trim();
  if (!isValidDeviceAlias(alias)) {
    sendError(400, "El alias debe tener el formato CCTV-0001");
    return;
  }
  if (preferences.putString("devalias", alias) == 0) {
    sendError(500, "No se pudo guardar el alias del equipo");
    return;
  }
  deviceAlias = alias;
  recordDeviceAliasEvent();
  String body = F("{\"saved\":true,\"alias\":\"");
  body += jsonEscape(deviceAlias);
  body += F("\"}");
  sendJson(200, body);
}

bool jsonStringField(const String &json, const char *name, String &value) {
  String key = "\"";
  key += name;
  key += '"';
  const int keyIndex = json.indexOf(key);
  if (keyIndex < 0) {
    return false;
  }
  int cursor = json.indexOf(':', keyIndex + key.length());
  if (cursor < 0) {
    return false;
  }
  ++cursor;
  while (cursor < static_cast<int>(json.length()) &&
         isspace(static_cast<unsigned char>(json[cursor]))) {
    ++cursor;
  }
  if (cursor >= static_cast<int>(json.length()) || json[cursor] != '"') {
    return false;
  }
  ++cursor;
  value = "";
  while (cursor < static_cast<int>(json.length()) && json[cursor] != '"') {
    const uint8_t c = static_cast<uint8_t>(json[cursor++]);
    if (c < 0x20 || c == '\\') {
      return false;
    }
    value += static_cast<char>(c);
  }
  return cursor < static_cast<int>(json.length()) && json[cursor] == '"';
}

bool jsonUnsignedField(const String &json, const char *name, size_t &value) {
  String key = "\"";
  key += name;
  key += '"';
  const int keyIndex = json.indexOf(key);
  if (keyIndex < 0) {
    return false;
  }
  int cursor = json.indexOf(':', keyIndex + key.length());
  if (cursor < 0) {
    return false;
  }
  ++cursor;
  while (cursor < static_cast<int>(json.length()) &&
         isspace(static_cast<unsigned char>(json[cursor]))) {
    ++cursor;
  }
  const char *start = json.c_str() + cursor;
  char *end = nullptr;
  const unsigned long parsed = strtoul(start, &end, 10);
  if (end == start || parsed > SIZE_MAX) {
    return false;
  }
  cursor = static_cast<int>(end - json.c_str());
  while (cursor < static_cast<int>(json.length()) &&
         isspace(static_cast<unsigned char>(json[cursor]))) {
    ++cursor;
  }
  if (cursor >= static_cast<int>(json.length()) ||
      (json[cursor] != ',' && json[cursor] != '}')) {
    return false;
  }
  value = static_cast<size_t>(parsed);
  return true;
}

bool parseFirmwareVersion(String version, uint16_t parts[3]) {
  version.trim();
  if (version.startsWith("v") || version.startsWith("V")) {
    version.remove(0, 1);
  }
  size_t cursor = 0;
  for (size_t part = 0; part < 3; ++part) {
    if (cursor >= version.length() || !isdigit(version[cursor])) {
      return false;
    }
    uint32_t number = 0;
    while (cursor < version.length() && isdigit(version[cursor])) {
      const uint32_t digit = static_cast<uint8_t>(version[cursor] - '0');
      if (number > (UINT16_MAX - digit) / 10U) {
        return false;
      }
      number = number * 10U + digit;
      ++cursor;
    }
    parts[part] = static_cast<uint16_t>(number);
    if (part < 2) {
      if (cursor >= version.length() || version[cursor++] != '.') {
        return false;
      }
    }
  }
  return cursor == version.length();
}

int compareFirmwareVersions(const uint16_t left[3], const uint16_t right[3]) {
  for (size_t i = 0; i < 3; ++i) {
    if (left[i] != right[i]) {
      return left[i] < right[i] ? -1 : 1;
    }
  }
  return 0;
}

bool readBoundedHttpBody(HTTPClient &http, String &body) {
  const int contentLength = http.getSize();
  if (contentLength <= 0 ||
      contentLength > static_cast<int>(OTA_MANIFEST_MAX_BYTES)) {
    return false;
  }
  auto *stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }
  body = "";
  body.reserve(contentLength);
  const uint32_t startedAt = millis();
  while (body.length() < static_cast<size_t>(contentLength)) {
    while (stream->available() &&
           body.length() < static_cast<size_t>(contentLength)) {
      const int nextByte = stream->read();
      if (nextByte < 0) {
        break;
      }
      body += static_cast<char>(nextByte);
    }
    if (body.length() >= static_cast<size_t>(contentLength)) {
      break;
    }
    if (!http.connected() && !stream->available()) {
      return false;
    }
    if (millis() - startedAt >= 10000UL) {
      return false;
    }
    delay(1);
  }
  return !body.isEmpty();
}

bool ensureSystemTime() {
  if (time(nullptr) >= 1700000000) {
    return true;
  }
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  const uint32_t startedAt = millis();
  while (millis() - startedAt < 20000UL) {
    if (time(nullptr) >= 1700000000) {
      return true;
    }
    delay(250);
  }
  return false;
}

void setOtaStatus(const String &status) {
  otaLastStatus = status;
  Serial.printf("OTA: %s\n", status.c_str());
}

bool performOtaCheck() {
  otaLatestVersion = "";
  otaAvailableVersion = "";
  otaFirmwareUrl = "";
  otaFirmwareSha256 = "";
  otaFirmwareSize = 0;
  if (otaManifestUrl.isEmpty()) {
    setOtaStatus("Actualizaciones OTA sin configurar");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    setOtaStatus("Esperando conexión Wi-Fi con Internet");
    return false;
  }
  if (!ensureSystemTime()) {
    setOtaStatus("No se pudo sincronizar la hora para validar HTTPS");
    return false;
  }

  NetworkClientSecure manifestTls;
  manifestTls.setTimeout(10000);
  manifestTls.setHandshakeTimeout(10);
  manifestTls.useBuiltinCACertBundle();
  HTTPClient manifestHttp;
  manifestHttp.setConnectTimeout(15000);
  manifestHttp.setTimeout(10000);
  manifestHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!manifestHttp.begin(manifestTls, otaManifestUrl)) {
    setOtaStatus("No se pudo iniciar la consulta HTTPS del manifiesto");
    return false;
  }
  const int manifestResponse = manifestHttp.GET();
  String manifest;
  if (manifestResponse != HTTP_CODE_OK) {
    manifestHttp.end();
    setOtaStatus("El manifiesto respondió HTTP " +
                 String(manifestResponse));
    return false;
  }
  if (!readBoundedHttpBody(manifestHttp, manifest)) {
    manifestHttp.end();
    setOtaStatus("El manifiesto está vacío, excede el límite o no terminó de leerse");
    return false;
  }
  manifestHttp.end();

  String version;
  String firmwareUrl;
  String sha256;
  size_t firmwareSize = 0;
  uint16_t currentParts[3] = {};
  uint16_t releaseParts[3] = {};
  if (!jsonStringField(manifest, "version", version) ||
      !jsonStringField(manifest, "url", firmwareUrl) ||
      !jsonStringField(manifest, "sha256", sha256) ||
      !jsonUnsignedField(manifest, "size", firmwareSize) ||
      !parseFirmwareVersion(version, releaseParts) ||
      !parseFirmwareVersion(FIRMWARE_VERSION, currentParts)) {
    setOtaStatus("El manifiesto OTA tiene campos o versión inválidos");
    return false;
  }
  otaLatestVersion = version;
  if (!firmwareUrl.startsWith("https://github.com/") ||
      firmwareUrl.indexOf("/releases/download/") < 0 ||
      sha256.length() != 64 || firmwareSize == 0 ||
      firmwareSize > ESP.getFreeSketchSpace()) {
    setOtaStatus("La URL, SHA-256 o tamaño del firmware no es válido");
    return false;
  }
  for (size_t i = 0; i < sha256.length(); ++i) {
    if (!isxdigit(static_cast<unsigned char>(sha256[i]))) {
      setOtaStatus("El SHA-256 del manifiesto no es hexadecimal");
      return false;
    }
  }
  if (compareFirmwareVersions(releaseParts, currentParts) <= 0) {
    setOtaStatus("Firmware actual " + String(FIRMWARE_VERSION) +
                 "; no hay una versión estable más nueva");
    return true;
  }

  otaAvailableVersion = version;
  otaFirmwareUrl = firmwareUrl;
  otaFirmwareSha256 = sha256;
  otaFirmwareSize = firmwareSize;
  setOtaStatus("Nueva versión " + version +
               " disponible; confirma la actualización en Resumen");
  return true;
}

void showOtaProgressScreen(int percent, bool full) {
  if (!displayReady) {
    return;
  }
  constexpr int barX = 10;
  constexpr int barW = 152;
  constexpr int barY = 150;
  constexpr int barH = 18;
  if (full) {
    display->fillScreen(RGB565_BLACK);
    display->setTextWrap(false);
    display->setTextSize(2);
    display->setTextColor(RGB565_YELLOW);
    display->setCursor(10, 20);
    display->println(F("ACTUALIZACION"));
    display->setCursor(10, 40);
    display->println(F("EN CURSO"));
    display->setTextSize(1);
    display->setTextColor(RGB565_WHITE);
    display->setCursor(10, 80);
    display->println(F("No reinicie ni apague"));
    display->setCursor(10, 94);
    display->println(F("el equipo"));
    display->drawRect(barX - 1, barY - 1, barW + 2, barH + 2, RGB565_WHITE);
  }
  percent = std::max(0, std::min(100, percent));
  const int filled = barW * percent / 100;
  display->fillRect(barX, barY, filled, barH, RGB565_GREEN);
  display->fillRect(barX + filled, barY, barW - filled, barH, RGB565_BLACK);
  display->fillRect(barX, barY + barH + 8, barW, 20, RGB565_BLACK);
  display->setTextSize(2);
  display->setTextColor(RGB565_CYAN);
  display->setCursor(barX, barY + barH + 8);
  display->printf("%d%%", percent);
  display->setTextSize(1);
}

void performOtaUpdate() {
  uint16_t currentParts[3] = {};
  uint16_t releaseParts[3] = {};
  if (otaAvailableVersion.isEmpty() || otaFirmwareUrl.isEmpty() ||
      otaFirmwareSha256.isEmpty() || otaFirmwareSize == 0 ||
      !parseFirmwareVersion(otaAvailableVersion, releaseParts) ||
      !parseFirmwareVersion(FIRMWARE_VERSION, currentParts) ||
      compareFirmwareVersions(releaseParts, currentParts) <= 0) {
    setOtaStatus("No hay una versión nueva válida para instalar");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    otaUpdateRequested = true;
    nextOtaCheckAt = millis() + 30000UL;
    setOtaStatus("Actualización confirmada; esperando conexión a Internet");
    return;
  }

  otaUpdateInProgress = true;
  otaProgressPercent = 0;
  showOtaProgressScreen(0, true);
  const auto failOtaUpdate = [](const String &status) {
    otaUpdateInProgress = false;
    updateDisplay();
    setOtaStatus(status);
  };

  const String version = otaAvailableVersion;
  const String firmwareUrl = otaFirmwareUrl;
  const String sha256 = otaFirmwareSha256;
  const size_t firmwareSize = otaFirmwareSize;
  setOtaStatus("Descargando firmware " + version + " desde GitHub");
  NetworkClientSecure firmwareTls;
  firmwareTls.setTimeout(10000);
  firmwareTls.setHandshakeTimeout(10);
  firmwareTls.useBuiltinCACertBundle();
  HTTPClient firmwareHttp;
  firmwareHttp.setConnectTimeout(20000);
  firmwareHttp.setTimeout(15000);
  firmwareHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!firmwareHttp.begin(firmwareTls, firmwareUrl)) {
    failOtaUpdate("No se pudo iniciar la descarga HTTPS del firmware");
    return;
  }
  const int firmwareResponse = firmwareHttp.GET();
  const int contentLength = firmwareHttp.getSize();
  if (firmwareResponse != HTTP_CODE_OK) {
    firmwareHttp.end();
    failOtaUpdate("La descarga del firmware respondió HTTP " +
                  String(firmwareResponse));
    return;
  }
  if (contentLength < 0 ||
      static_cast<size_t>(contentLength) != firmwareSize) {
    firmwareHttp.end();
    failOtaUpdate(
        "El tamaño recibido desde GitHub no coincide con el manifiesto");
    return;
  }
  if (!Update.begin(firmwareSize, U_FLASH)) {
    const String error = Update.errorString();
    Update.abort();
    firmwareHttp.end();
    failOtaUpdate("No se pudo preparar la partición OTA: " + error);
    return;
  }
  if (!Update.setSHA256(sha256.c_str())) {
    Update.abort();
    firmwareHttp.end();
    failOtaUpdate("No se pudo configurar la verificación SHA-256");
    return;
  }
  int lastShownPercent = 0;
  uint32_t lastPortalServiceAt = 0;
  auto *firmwareStream = firmwareHttp.getStreamPtr();
  size_t written = 0;
  if (firmwareStream != nullptr) {
    uint8_t buffer[1024];
    const uint32_t startedAt = millis();
    uint32_t lastDataAt = startedAt;
    while (written < firmwareSize) {
      const size_t available = firmwareStream->available();
      if (available == 0) {
        if (millis() - lastDataAt >= 15000UL ||
            millis() - startedAt >= 300000UL ||
            (!firmwareHttp.connected() && !firmwareStream->available())) {
          break;
        }
        delay(2);
        continue;
      }
      const size_t toRead =
          std::min({available, sizeof(buffer), firmwareSize - written});
      const int bytesRead = firmwareStream->readBytes(buffer, toRead);
      if (bytesRead <= 0) {
        break;
      }
      if (Update.write(buffer, bytesRead) != static_cast<size_t>(bytesRead)) {
        break;
      }
      written += bytesRead;
      const int percent = static_cast<int>(written * 100 / firmwareSize);
      if (percent != lastShownPercent) {
        lastShownPercent = percent;
        otaProgressPercent = percent;
        showOtaProgressScreen(percent, false);
      }
      if (millis() - lastPortalServiceAt >= 300UL) {
        lastPortalServiceAt = millis();
        server.handleClient();
      }
      lastDataAt = millis();
      yield();
    }
  }
  firmwareHttp.end();
  if (written != firmwareSize) {
    Update.abort();
    otaUpdateInProgress = false;
    updateDisplay();
    setOtaStatus("Descarga incompleta: recibidos " + String(written) +
                 " de " + String(firmwareSize) + " bytes");
    return;
  }
  if (!Update.end() || !Update.isFinished()) {
    const String error = Update.errorString();
    Update.abort();
    otaUpdateInProgress = false;
    updateDisplay();
    setOtaStatus("Falló la verificación SHA-256 o la activación OTA: " + error);
    return;
  }
  setOtaStatus("Firmware " + version +
               " verificado; reiniciando para probarlo con rollback");
  delay(1000);
  ESP.restart();
}

void handleOtaStatus() {
  if (!requireSession()) {
    return;
  }
  String body = F("{\"currentVersion\":\"");
  body += jsonEscape(FIRMWARE_VERSION);
  body += F("\",\"manifestUrl\":\"");
  body += jsonEscape(otaManifestUrl);
  body += F("\",\"latestVersion\":\"");
  body += jsonEscape(otaLatestVersion);
  body += F("\",\"availableVersion\":\"");
  body += jsonEscape(otaAvailableVersion);
  body += F("\",\"updateAvailable\":");
  body += otaAvailableVersion.isEmpty() ? F("false") : F("true");
  body += F(",\"status\":\"");
  body += jsonEscape(otaLastStatus);
  body += F("\",\"updateRequested\":");
  body += otaUpdateRequested ? F("true") : F("false");
  body += F(",\"updating\":");
  body += otaUpdateInProgress ? F("true") : F("false");
  body += F(",\"progress\":");
  body += String(otaProgressPercent);
  body += F("}");
  sendJson(200, body);
}

void handleOtaConfig() {
  if (!requireSession()) {
    return;
  }
  if (!preferencesReady) {
    sendError(500, "No se puede guardar la configuración OTA en NVS");
    return;
  }
  String url = server.arg("url");
  url.trim();
  if (!url.isEmpty() && !isValidOtaManifestUrl(url)) {
    sendError(400, "Usa la URL HTTPS raw.githubusercontent.com del manifiesto estable");
    return;
  }
  if (url != otaManifestUrl) {
    if (!otaConfigAuthorized ||
        static_cast<int32_t>(otaConfigAuthorizationUntil - millis()) <= 0) {
      otaConfigAuthorized = false;
      sendError(403, "Mantén BOOT durante 1 segundo y vuelve a guardar en menos de 30 segundos");
      return;
    }
    otaConfigAuthorized = false;
    otaConfigAuthorizationUntil = 0;
  }
  if (url.isEmpty()) {
    preferences.remove("otamanifest");
    otaManifestUrl = "";
    otaLatestVersion = "";
    otaAvailableVersion = "";
    otaFirmwareUrl = "";
    otaFirmwareSha256 = "";
    otaFirmwareSize = 0;
    otaCheckRequested = false;
    otaUpdateRequested = false;
    setOtaStatus("Actualizaciones OTA desactivadas");
  } else {
    if (preferences.putString("otamanifest", url) == 0) {
      sendError(500, "No se pudo guardar la URL del manifiesto OTA");
      return;
    }
    otaManifestUrl = url;
    otaLatestVersion = "";
    otaAvailableVersion = "";
    otaFirmwareUrl = "";
    otaFirmwareSha256 = "";
    otaFirmwareSize = 0;
    otaCheckRequested = true;
    setOtaStatus("Configuración guardada; comprobación OTA en curso");
  }
  sendJson(200, "{\"saved\":true}");
}

void handleOtaCheck() {
  if (!requireSession()) {
    return;
  }
  if (otaManifestUrl.isEmpty()) {
    sendError(400, "Configura primero la URL pública del manifiesto OTA");
    return;
  }
  if (otaUpdateInProgress) {
    sendError(409, "Ya hay una actualización OTA en curso");
    return;
  }
  otaCheckRequested = true;
  setOtaStatus("Comprobación manual solicitada");
  sendJson(202, "{\"queued\":true}");
}

void handleOtaUpdate() {
  if (!requireSession()) {
    return;
  }
  if (server.arg("confirm") != "ACTUALIZAR") {
    sendError(400, "Confirma la actualización desde la página Resumen");
    return;
  }
  if (otaUpdateInProgress || otaUpdateRequested) {
    sendError(409, "Ya hay una actualización OTA solicitada o en curso");
    return;
  }
  if (otaManifestUrl.isEmpty() || otaAvailableVersion.isEmpty()) {
    sendError(409, "No hay una versión nueva disponible para instalar");
    return;
  }
  otaUpdateRequested = true;
  nextOtaCheckAt = millis();
  setOtaStatus("Actualización de " + otaAvailableVersion +
               " confirmada; preparando descarga");
  sendJson(202, "{\"queued\":true}");
}

void verifyPendingOtaImage() {
#if defined(CONFIG_APP_ROLLBACK_ENABLE)
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running == nullptr ||
      esp_ota_get_state_partition(running, &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }
  if (!displayReady || !filesystemReady || !accessPointReady) {
    recordError("OTA", "La prueba de arranque falló; se revierte el firmware");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return;
  }
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result != ESP_OK) {
    recordError("OTA", "No se pudo confirmar el firmware; se revierte");
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return;
  }
  Serial.println(F("OTA: arranque verificado; rollback cancelado"));
#endif
}

void initializeDisplay() {
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);
  if (!display->begin()) {
    Serial.println(F("ERROR: Arduino_GFX no pudo inicializar la pantalla"));
    recordError("LCD", "Arduino_GFX no pudo inicializar la pantalla");
    return;
  }
  displayReady = true;
  display->fillScreen(RGB565_BLACK);
  updateDisplay();
}

void initializeStaReconnect() {
  if (savedNetworks.empty()) {
    return;
  }
  nextReconnectAt = millis();
  reconnectIndex = 0;
  maintainStaConnection();
}

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#101827">
<title>ESP32 Site Survey</title>
<style>
:root{color-scheme:dark;--bg:#0b1220;--panel:#131e30;--line:#263650;--text:#e9f0fb;--muted:#9aabc2;--cyan:#5de1e6;--green:#73e0ad;--amber:#ffc66d;--red:#ff7d8a}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at 20% 0,#172d44 0,transparent 42%),var(--bg);color:var(--text);font:15px/1.5 system-ui,-apple-system,Segoe UI,sans-serif}
.wrap{width:min(1120px,100%);margin:auto;padding:20px}.top{display:flex;align-items:center;justify-content:space-between;gap:14px;margin-bottom:18px}.brand{display:flex;align-items:center;gap:12px}.logo{width:42px;height:42px;border-radius:13px;background:linear-gradient(135deg,#62e8dd,#5e8cff);display:grid;place-items:center;color:#071321;font-weight:900}.brand h1{font-size:19px;margin:0}.brand p{margin:2px 0 0;color:var(--muted);font-size:12px}.pill{border:1px solid var(--line);border-radius:999px;padding:6px 11px;color:var(--muted);font-size:12px}.pill.ok{color:var(--green);border-color:#34775d}.pill.warn{color:var(--amber);border-color:#765b31}
.nav{display:flex;gap:8px;overflow:auto;border-bottom:1px solid var(--line);margin-bottom:18px}.nav button{white-space:nowrap;background:transparent;color:var(--muted);border:0;border-bottom:2px solid transparent;padding:12px 14px;cursor:pointer;font:inherit}.nav button.active{color:var(--cyan);border-color:var(--cyan)}
.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}.card{background:linear-gradient(160deg,rgba(24,38,59,.96),rgba(17,27,43,.96));border:1px solid var(--line);border-radius:16px;padding:17px;box-shadow:0 12px 34px rgba(0,0,0,.12)}.card h2,.card h3{margin:0 0 12px}.card h2{font-size:17px}.card h3{font-size:14px;color:var(--muted)}.span12{grid-column:span 12}.span8{grid-column:span 8}.span6{grid-column:span 6}.span4{grid-column:span 4}
.metric{font-size:24px;font-weight:750;letter-spacing:-.4px}.muted{color:var(--muted)}.small{font-size:12px}.row{display:flex;align-items:center;gap:9px;flex-wrap:wrap}.spread{justify-content:space-between}.stack{display:grid;gap:10px}.statgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}.stat{background:rgba(7,14,25,.35);border:1px solid var(--line);border-radius:12px;padding:12px}.stat label{display:block;color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.08em}.stat strong{display:block;margin-top:3px;font-size:16px;overflow-wrap:anywhere}
.signal-good{color:var(--green)}.signal-medium{color:var(--amber)}.signal-poor{color:var(--red)}.signal-unknown{color:var(--muted)}
button,.button{background:linear-gradient(135deg,#5de1e6,#74a7ff);border:0;border-radius:10px;color:#071321;padding:10px 14px;cursor:pointer;font:inherit;font-weight:700;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;gap:7px}button.secondary,.button.secondary{background:#22334b;color:var(--text);border:1px solid #344862}button.danger{background:#462832;color:#ffb6bf;border:1px solid #70424b}button:disabled{opacity:.5;cursor:wait}
input,select{width:100%;background:#0d1727;color:var(--text);border:1px solid #344762;border-radius:9px;padding:11px 12px;font:inherit;outline:none}input:focus,select:focus{border-color:var(--cyan)}label{display:grid;gap:5px;color:var(--muted);font-size:12px}
.formgrid{display:grid;grid-template-columns:1.5fr 1.5fr auto;align-items:end;gap:10px}.asset-formgrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));align-items:end;gap:10px}textarea{width:100%;min-height:110px;resize:vertical;background:#0d1727;color:var(--text);border:1px solid #344762;border-radius:9px;padding:11px 12px;font:inherit}.notice{border-left:3px solid var(--amber);padding:9px 12px;background:#30291d;color:#efd7a7;border-radius:0 9px 9px 0;font-size:12px}
.tablewrap{overflow:auto;border:1px solid var(--line);border-radius:10px}table{width:100%;border-collapse:collapse;min-width:650px;font-size:13px}th,td{text-align:left;padding:10px 11px;border-bottom:1px solid var(--line);vertical-align:top}th{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.06em;background:#101a29}tr:last-child td{border-bottom:0}.risk-high{color:var(--red)}.risk-protected{color:var(--green)}.risk-standard{color:var(--amber)}
.storage-stack{display:grid;gap:14px}.storage-item{display:grid;gap:6px}.storage-summary{color:var(--muted);font-size:12px}.storage-bar{height:18px;display:flex;overflow:hidden;border:1px solid var(--line);border-radius:999px;background:#0d1727}.storage-used{height:100%;background:#ffd43b}.storage-free{height:100%;background:#e347dd}.storage-legend{display:flex;gap:14px;flex-wrap:wrap;color:var(--muted);font-size:11px}.storage-legend span{display:inline-flex;align-items:center;gap:6px}.storage-swatch{width:10px;height:10px;border-radius:3px;display:inline-block}.storage-swatch.used{background:#ffd43b}.storage-swatch.free{background:#e347dd}
.sd-format-progress{height:8px;overflow:hidden;border:1px solid var(--line);border-radius:999px;background:#0d1727}.sd-format-progress span{display:block;width:35%;height:100%;background:var(--cyan);animation:sd-format-slide 1.2s ease-in-out infinite alternate}@keyframes sd-format-slide{from{transform:translateX(0)}to{transform:translateX(185%)}}
.tabs{display:none}.tabs.active{display:block}.canvasbox{width:100%;overflow:hidden}canvas{width:100%;height:280px;display:block;background:#0d1727;border:1px solid var(--line);border-radius:10px}.spectrum-legend{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:7px;margin-top:10px}.legend-item{display:flex;align-items:center;gap:8px;min-width:0;padding:7px 9px;background:#101a29;border:1px solid var(--line);border-radius:8px}.legend-swatch{flex:0 0 11px;width:11px;height:11px;border-radius:50%}.legend-text{overflow-wrap:anywhere}
.error{color:var(--red);min-height:20px;font-size:13px}.error-log{max-height:320px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere;background:#0d1727;border:1px solid var(--line);border-radius:9px;padding:12px}.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);background:#263b52;border:1px solid #41627f;border-radius:10px;padding:10px 15px;z-index:10;display:none;max-width:90vw;align-items:center;justify-content:center;flex-wrap:wrap;gap:10px}.toast.show{display:flex}.toast span{flex:1 1 180px;user-select:text;overflow-wrap:anywhere}.hidden{display:none!important}.ota-overlay{position:fixed;inset:0;background:#000;z-index:1000;display:flex;align-items:center;justify-content:center;padding:20px}.ota-box{width:min(560px,100%);text-align:center}.ota-track{height:26px;border:1px solid #fff;border-radius:6px;overflow:hidden;margin:18px 0 8px}.ota-fill{height:100%;width:0;background:#2ecc71;transition:width .4s}.ota-pct{font-size:28px;font-weight:700;color:#5fd0ff;margin:0 0 14px}.foot{color:var(--muted);font-size:11px;text-align:center;padding:20px 0}
@media(max-width:760px){.wrap{padding:14px}.span8,.span6,.span4{grid-column:span 12}.statgrid{grid-template-columns:repeat(2,1fr)}.formgrid,.asset-formgrid{grid-template-columns:1fr}.formgrid button,.asset-formgrid button{width:100%}.top{align-items:flex-start}.pill{font-size:11px}.card{padding:14px}canvas{height:240px}}
</style>
</head>
<body>
<div class="wrap">
  <header class="top">
    <div class="brand"><div class="logo">S</div><div><h1>ESP32 Site Survey</h1><p>Waveshare ESP32-S3-LCD-1.47B · auditoría pasiva</p></div></div>
    <div class="row"><span id="connectionPill" class="pill warn">Sin STA</span></div>
  </header>
  <nav class="nav">
    <button class="active" data-tab="overview">Resumen</button>
    <button data-tab="wifi">Wi-Fi y espectro</button>
    <button data-tab="test">Prueba de enlace</button>
    <button data-tab="devices">Dispositivos LAN</button>
    <button data-tab="cctv">Inventario CCTV</button>
    <button data-tab="points">Puntos guardados</button>
  </nav>
  <section class="tabs active" id="tab-overview">
    <div class="grid">
      <article class="card span12"><h2>Estado del sistema</h2><div class="statgrid">
        <div class="stat"><label>Punto de acceso</label><strong id="apName">--</strong></div>
        <div class="stat"><label>Portal</label><strong id="apIp">--</strong></div>
        <div class="stat"><label>Red STA</label><strong id="staName">--</strong></div>
        <div class="stat"><label>Señal STA</label><strong id="staRssi">--</strong></div>
        <div class="stat"><label>IP del ESP32 en el router</label><strong id="staIp">--</strong></div>
        <div class="stat"><label>Resolución MAC del gateway</label><strong id="gatewayManufacturer">--</strong><span id="gatewayMacAddress" class="muted small">MAC: --</span><span id="gatewayMacLookupStatus" class="muted small">Esperando conexión STA…</span><a id="gatewayMacLookupLink" class="muted small" hidden target="_blank" rel="noopener noreferrer">Abrir consulta por MAC</a></div>
      </div><p id="statusMessage" class="muted small">Cargando estado…</p><p id="networkError" class="error"></p></article>
      <article class="card span12"><h2>Identidad del equipo</h2><p class="muted small">El ID técnico se obtiene de la MAC de fábrica grabada en eFuse. El alias es una etiqueta de inventario editable; por sí solo no autentica el equipo ni evita una clonación.</p><div class="statgrid"><div class="stat"><label>ID técnico</label><strong id="deviceId">Consultando…</strong></div><div class="stat"><label>Alias de inventario</label><strong id="deviceAliasDisplay">Sin asignar</strong></div><div class="stat"><label>Firmware</label><strong id="deviceFirmware">--</strong></div><div class="stat"><label>Chip</label><strong id="deviceHardware">--</strong></div><div class="stat"><label>Flash / PSRAM</label><strong id="deviceMemory">--</strong></div><div class="stat"><label>Arranques / último reset</label><strong id="deviceBootInfo">--</strong></div></div><form id="deviceAliasForm" class="row"><label>Alias de inventario<input id="deviceAliasInput" type="text" minlength="9" maxlength="13" pattern="CCTV-[0-9]{4,8}" placeholder="CCTV-0001" autocomplete="off"></label><button id="saveDeviceAliasBtn" type="submit">Guardar alias</button></form><p id="deviceAliasFeedback" class="muted small" aria-live="polite">Asigna CCTV-0001 a esta primera placa. El ID técnico seguirá siendo el identificador de hardware.</p></article>
      <article class="card span12"><h2>Almacenamiento</h2><p class="muted small">Espacio para archivos en LittleFS y en la microSD. No representa la RAM ni todo el espacio de firmware del chip.</p><div class="storage-stack"><div class="storage-item"><strong>ESP32 · LittleFS</strong><div id="espStorageSummary" class="storage-summary">Consultando…</div><div id="espStorageBar" class="storage-bar" role="progressbar" aria-label="Almacenamiento LittleFS usado y libre" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"></div></div><div class="storage-item"><strong>microSD</strong><div id="sdStorageSummary" class="storage-summary">Consultando…</div><div id="sdStorageBar" class="storage-bar" role="progressbar" aria-label="Almacenamiento microSD usado y libre" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"></div><button id="formatSdBtn" class="danger" type="button" disabled>Formatear microSD</button><p id="sdFormatFeedback" class="muted small" aria-live="polite">El formato borra todos los archivos de la microSD.</p><div id="sdFormatProgress" class="sd-format-progress hidden" role="progressbar" aria-label="Formato de microSD en curso"><span></span></div></div></div><div class="storage-legend"><span><i class="storage-swatch used"></i>Usado</span><span><i class="storage-swatch free"></i>Libre</span></div><p id="storageStatus" class="muted small">Comprobando almacenamiento…</p></article>
      <article class="card span12"><h2>Configuración Bluetooth BLE</h2><p id="bleStatus" class="muted small">Consultando estado de BLE…</p></article>
      <article class="card span12"><h2>Actualizaciones OTA desde GitHub</h2><p class="muted small">La ESP32 consulta por HTTPS el manifiesto de versiones estables al conectarse a Internet y luego cada 6 horas. La comprobación no instala firmware. Si encuentra una versión más nueva, aparecerá aquí; la descarga solo comienza cuando confirmes la actualización. Se valida el tamaño y SHA-256 y, si falla la prueba básica de arranque, el firmware anterior se restaura. Deja la URL vacía para desactivar las comprobaciones. Por seguridad, para cambiar o borrar la URL mantén BOOT durante 1 segundo y guarda en menos de 30 segundos.</p><form id="otaConfigForm" class="stack"><label>URL HTTPS del manifiesto estable<input id="otaManifestUrl" type="url" maxlength="255" autocomplete="url" placeholder="https://raw.githubusercontent.com/usuario/repositorio/main/ota/stable.json"></label><div class="row"><button id="otaSaveBtn" type="submit">Guardar y comprobar</button><button id="otaCheckBtn" class="secondary" type="button">Comprobar ahora</button></div></form><button id="otaUpdateBtn" type="button" disabled>Actualizar dispositivo</button><p id="otaAvailability" class="muted small" aria-live="polite">Esperando comprobación de actualizaciones…</p><p id="otaVersion" class="muted small">Versión actual: consultando…</p><p id="otaStatus" class="muted small" aria-live="polite">Consultando estado OTA…</p></article>
      <article class="card span6"><div class="row spread"><h2>Conectar una red autorizada</h2><button class="secondary" id="scanBtnOverview">Escanear redes</button></div><p class="muted small">Escanea, selecciona tu red e introduce su clave. Se guardará en la lista blanca únicamente tras conectar correctamente.</p><p id="scanFeedback" class="muted small">Todavía no se ha realizado un escaneo.</p><div class="stack">
        <label>Redes encontradas<select id="networkSelect"><option value="">Escanea para ver redes</option></select></label>
        <label>Contraseña Wi-Fi<input id="wifiPassword" type="password" autocomplete="off" maxlength="64" placeholder="Vacía si la red es abierta"></label>
        <button id="connectBtn">Conectar y guardar</button>
      </div></article>
      <article class="card span6"><h2>Lista blanca</h2><p class="muted small">Redes guardadas en Preferences/NVS para reconexión automática. Las claves no se muestran en el portal.</p><div id="whitelist" class="stack muted small">Sin redes guardadas.</div></article>
      <article class="card span12"><div class="notice">El escaneo escucha anuncios Wi-Fi de forma pasiva; no intenta entrar, interrumpir ni probar redes. Úsalo solo en redes propias o con autorización.</div></article>
      <article class="card span12"><div class="row spread"><div><h2>LOG de errores de la ESP32</h2><p id="errorLogSummary" class="muted small">Consultando el registro local…</p></div><div class="row"><button id="refreshErrorLogBtn" class="secondary" type="button">Actualizar</button><a class="button secondary" href="/api/error-log">Descargar LOG</a></div></div><p class="muted small">Se guarda en LittleFS (hasta 32 KB, con rotación). Al abrir la conexión USB Serial, la placa lo envía entre marcadores de inicio y fin.</p><pre id="errorLogContent" class="error-log muted small">Consultando…</pre></article>
    </div>
  </section>
  <section class="tabs" id="tab-wifi">
    <div class="grid">
      <article class="card span12"><div class="row spread"><div><h2>Redes cercanas y canales</h2><p class="muted small">Escaneo pasivo en 2.4 GHz · canales 1–13 · la evaluación se basa solo en el cifrado anunciado.</p></div><div class="row"><button id="scanBtn">Escanear redes</button><a href="/api/scan.csv">Descargar CSV</a></div></div><div class="canvasbox"><canvas id="spectrum" width="1000" height="320"></canvas></div><div id="spectrumLegend" class="spectrum-legend muted small">Los SSID aparecerán aquí después del escaneo.</div><div class="notice" id="channelAdvice">Escanea redes para comparar los canales 1, 6 y 11.</div><div class="muted small">La recomendación estima solapamiento de señales anunciadas; no mide tráfico real ni cambia la configuración del router.</div></article>
      <article class="card span12"><h2>Auditoría pasiva</h2><div class="tablewrap"><table><thead><tr><th>SSID</th><th>BSSID</th><th>Canal</th><th>RSSI</th><th>Cifrado</th><th>Evaluación</th></tr></thead><tbody id="scanTable"><tr><td colspan="6" class="muted">Pulsa “Escanear redes”.</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="tabs" id="tab-test">
    <div class="grid">
      <article class="card span8"><h2>Medición hacia el router e Internet</h2><p class="muted small">Cada prueba mide la respuesta del gateway por ICMP, la señal Wi-Fi de la ESP32 y la descarga de Internet en speed.cloudflare.com. El gateway se mide en ms, no en Mbps: para una tasa local de transferencia se necesita un servidor de prueba activo en el router. La velocidad externa incluye Wi-Fi y proveedor, y consume datos.</p><div class="formgrid">
        <label>Tamaño de descarga<select id="testSize"><option value="1048576">1 MB</option><option value="3145728" selected>3 MB</option><option value="5242880">5 MB</option></select></label>
        <label>Nombre del punto<input id="pointName" maxlength="32" value="Camara_Entrada" placeholder="Ej. Camara_Entrada"></label>
        <button id="runTestBtn">Medir y registrar</button>
      </div><p id="testMessage" class="muted small">Listo para medir router, señal Wi-Fi e Internet desde la ESP32.</p><div class="notice" id="analysisResult">Señal: buena ≥ -60 dBm; media de -61 a -70 dBm; mala &lt; -70 dBm. La medición al router muestra respuesta ICMP, no tasa Mbps.</div></article>
      <article class="card span4"><h3>Últimos resultados</h3><div class="stack"><div><div class="metric" id="speedResult">-- Mbps</div><div class="muted small">Descarga de Internet</div></div><div><strong id="gatewayLatencyResult">-- ms</strong><div class="muted small">Respuesta ICMP del router</div></div><div><strong id="signalResult" class="signal-unknown">-- dBm</strong><div class="muted small">Señal Wi-Fi STA</div></div><div><strong id="latencyResult">-- ms</strong><div class="muted small">Respuesta HTTP externa</div></div></div></article>
    </div>
  </section>
  <section class="tabs" id="tab-devices">
    <div class="grid">
      <article class="card span12"><h2>Ping de la puerta de enlace</h2><p class="muted small">Envía un único ping ICMP a la puerta de enlace de la red STA conectada.</p><div class="row"><button type="button" class="secondary" id="gatewayPingBtn">Probar gateway</button><span id="gatewayPingResult" class="muted small">Conecta la STA para realizar la prueba.</span></div></article>
      <article class="card span12"><div class="row spread"><div><h2>Descubrimiento Hikvision / EZVIZ (SADP)</h2><p class="muted small">Envía una consulta multicast SADP a la LAN Wi-Fi conectada y escucha respuestas durante 3 segundos. Solo muestra dispositivos que respondan; no prueba acceso ni credenciales. El multicast puede estar bloqueado por el router, aislamiento Wi-Fi o VLAN.</p></div><div class="row"><button type="button" class="secondary" id="sadpDiscoverBtn">Buscar dispositivos SADP</button><button type="button" class="secondary" id="sadpHtmlBtn" data-html-check="sadpTable">Comprobar HTML</button></div></div><p id="sadpFeedback" class="muted small">La búsqueda es manual y solo se ejecuta al pulsar el botón.</p><div class="tablewrap"><table><thead><tr><th>IP</th><th>MAC</th><th>Modelo / tipo</th><th>Firmware</th><th>Servidor HTML</th></tr></thead><tbody id="sadpTable"><tr><td colspan="5" class="muted">Todavía no se realizó una búsqueda SADP.</td></tr></tbody></table></div></article>
      <article class="card span12"><div class="row spread"><div><h2>Descubrimiento Dahua (DHDiscover)</h2><p class="muted small">Envía una consulta DHDiscover multicast a la LAN Wi-Fi conectada y escucha respuestas durante 3 segundos. Solo muestra los datos identificativos de los dispositivos que respondan; no prueba acceso ni credenciales. El multicast puede estar bloqueado por el router, aislamiento Wi-Fi o VLAN.</p></div><div class="row"><button type="button" class="secondary" id="dhDiscoverBtn">Buscar dispositivos Dahua</button><button type="button" class="secondary" id="dhHtmlBtn" data-html-check="dhDiscoverTable">Comprobar HTML</button></div></div><p id="dhDiscoverFeedback" class="muted small">La búsqueda es manual y solo se ejecuta al pulsar el botón.</p><div class="tablewrap"><table><thead><tr><th>IP</th><th>MAC</th><th>Modelo</th><th>Clase</th><th>Firmware</th><th>Número de serie</th><th>Servidor HTML</th></tr></thead><tbody id="dhDiscoverTable"><tr><td colspan="7" class="muted">Todavía no se realizó una búsqueda DHDiscover.</td></tr></tbody></table></div></article>
      <article class="card span12"><div class="row spread"><div><h2>Dispositivos genéricos</h2><p class="muted small">Consulta ONVIF/WS-Discovery, SSDP/UPnP y mDNS/DNS-SD en la LAN Wi-Fi conectada. Excluye SADP de Hikvision/EZVIZ y DHDiscover de Dahua. Solo envía consultas multicast; no barre rangos, prueba puertos ni accede a cámaras.</p></div><div class="row"><button type="button" class="secondary" id="genericDiscoverBtn">BUSCAR DISPOSITIVOS GENÉRICOS</button><button type="button" class="secondary" id="genericHtmlBtn" data-html-check="genericDiscoverTable">Comprobar HTML</button></div></div><p id="genericDiscoverFeedback" class="muted small">La búsqueda es manual y puede tardar hasta 6 segundos; solo se ejecuta al pulsar el botón.</p><div class="tablewrap"><table><thead><tr><th>IP</th><th>Protocolo</th><th>Dispositivo / servicio</th><th>Endpoint anunciado</th><th>Servidor HTML</th></tr></thead><tbody id="genericDiscoverTable"><tr><td colspan="5" class="muted">Todavía no se realizó una búsqueda genérica.</td></tr></tbody></table></div></article>
      <article class="card span12"><div class="row spread"><div><h2>Dispositivos de la LAN</h2><p class="muted small">Combina ARP observado, reservas del gateway e inventario importado; la columna “Origen” distingue cada fuente. No barre la red. Las reservas no demuestran que un equipo siga conectado; para ver clientes DHCP actuales, importa la lista exportada desde el router.</p></div><div class="row"><button id="refreshDevicesBtn">Actualizar dispositivos</button><button type="button" id="checkHttpServersBtn" class="secondary">Corroborar HTML</button><a class="button secondary" href="/api/arp-neighbors.csv">Descargar CSV ARP</a></div></div><p id="lanDevicesFeedback" class="muted small">Pulsa “Actualizar dispositivos” para cargar las observaciones e IP importadas.</p><p id="httpCheckFeedback" class="muted small">Al pulsar, se cargan IP conocidas y se envía GET / en los puertos 80 y 8080 de la subred STA.</p><div class="row"><button type="button" id="stopDevicePingBtn" class="danger" disabled>Finalizar ping</button><span id="devicePingFeedback" class="muted small">Selecciona un dispositivo para iniciar un ping acotado.</span></div><div class="tablewrap"><table><thead><tr><th>IP</th><th>Red</th><th>MAC</th><th>Nombre</th><th>Origen</th><th>Estado</th><th>Servidor HTTP/80</th><th>Ping</th></tr></thead><tbody id="lanDevicesTable"><tr><td colspan="8" class="muted">Todavía no hay datos cargados.</td></tr></table></div><p class="muted small">“Abrir HTTP” aparece si una IP conocida responde en los puertos permitidos con Content-Type HTML o si la respuesta contiene estructura HTML. No se buscan otros puertos ni se enlazan servicios no confirmados. El ping se ejecuta individualmente; una falta de respuesta ICMP no confirma que el equipo esté apagado.</p></article>
      <article class="card span12"><h2>Revisión defensiva manual</h2><p id="assetGuidance" class="muted small">Recomendaciones generales para revisar los equipos CCTV desde el router o su interfaz oficial. No se realizan pruebas de acceso, vulnerabilidad ni firmware.</p><div class="notice">En el router o en la interfaz oficial del equipo, revisa si UPnP está desactivado cuando no sea necesario, cambia las claves predeterminadas por claves únicas, limita el acceso remoto y comprueba la versión de firmware con el soporte oficial del fabricante. No descargues ni instales firmware de fuentes no oficiales.</div></article>
      <article class="card span12"><h3>Resultados de comprobación web</h3><p class="muted small">Resultados de IP observadas por ARP o importadas. Se comprueba solo HTTP/80; no se exploran rangos ni otros puertos. La latencia es la conexión TCP medida por la ESP32.</p><div class="tablewrap"><table><thead><tr><th>IP</th><th>Puerto</th><th>Conexión</th><th>Latencia TCP</th><th>Respuesta</th><th>Enlace</th></tr></thead><tbody id="httpCheckResultsTable"><tr data-empty="true"><td colspan="6" class="muted">Todavía no hay comprobaciones.</td></tr></tbody></table></div></article>
      <article class="card span12"><div class="row spread"><div><h2>Redes virtuales / SSID visibles</h2><p class="muted small">Escaneo pasivo de 2.4 GHz. Los SSID de invitados o virtuales aparecerán si se anuncian; ver un SSID no prueba que pertenezca al router conectado. Esta ESP32 no puede identificar VLAN aisladas ni mostrar sus clientes sin información del router.</p></div><button type="button" id="scanLanNetworksBtn" class="secondary">Buscar redes Wi-Fi</button></div><p id="lanNetworksFeedback" class="muted small">Pulsa “Buscar redes Wi-Fi” para listar SSID visibles en 2.4 GHz.</p><div class="tablewrap"><table><thead><tr><th>SSID</th><th>BSSID</th><th>Canal</th><th>Señal</th><th>Seguridad anunciada</th><th>Identificación</th></tr></thead><tbody id="lanNetworksTable"><tr><td colspan="6" class="muted">Todavía no hay un escaneo.</td></tr></tbody></table></div></article>
      <article class="card span12"><div class="row spread"><div><h2>Historial de descubrimientos en microSD</h2><p class="muted small">Se guardan las respuestas SADP, DHDiscover y genéricas con fecha y hora del navegador, SSID y gateway. Se muestran los 200 registros más recientes; el archivo CSV de la tarjeta conserva el historial completo.</p></div><button id="refreshDiscoveryHistoryBtn" class="secondary" type="button">Actualizar historial</button></div><p id="discoveryHistoryStatus" class="muted small">Consultando historial de descubrimientos…</p><div class="tablewrap"><table><thead><tr><th>Fecha / hora</th><th>Origen</th><th>IP</th><th>Protocolo</th><th>Identificación / modelo</th><th>MAC</th><th>Firmware / serie</th><th>Red / gateway</th><th>Endpoint</th></tr></thead><tbody id="discoveryHistoryTable"><tr><td colspan="9" class="muted">Consultando historial…</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="tabs" id="tab-cctv">
    <div class="grid">
      <article class="card span12"><h2>Inventario de equipos CCTV</h2><p class="muted small">Selecciona una IP observada o una reserva conocida. “Ninguna” registra el equipo sin IP. “Ingresar IP manualmente” guarda una IP temporal, sin cotejarla ni probarla contra las tablas de red hasta que el equipo esté instalado.</p><div class="statgrid"><div class="stat"><label>Puerta de enlace</label><strong id="assetGateway">--</strong></div><div class="stat"><label>Red Wi-Fi conectada</label><strong id="assetNetwork">--</strong></div></div><form id="assetForm" class="asset-formgrid">
        <label>IP local<select id="assetIpSelect" required><option value="" disabled selected>Consultando IP detectadas…</option></select><input id="assetIpManual" name="ip" inputmode="url" autocapitalize="off" spellcheck="false" maxlength="15" placeholder="IP temporal, ej. 192.168.1.25" disabled hidden><span id="assetIpFeedback" class="muted small">Consultando IP detectadas…</span></label>
        <label>Nombre del equipo<input name="name" maxlength="32" placeholder="Camara_Entrada" required></label>
        <label>Tipo<select name="type" required><option value="Camara">Cámara</option><option value="NVR">NVR</option><option value="DVR">DVR</option><option value="Otro">Otro</option></select></label>
        <label>MAC<input name="mac" maxlength="17" placeholder="Opcional · AA:BB:CC:DD:EE:FF"></label>
        <label>Fabricante declarado<select name="manufacturer" required><option value="">Selecciona fabricante</option><option value="HIKVISION">HIKVISION</option><option value="DAHUA">DAHUA</option><option value="EZVIZ">EZVIZ</option><option value="OTRAS">OTRAS</option></select></label>
        <label>Modelo<input name="model" maxlength="32" required></label>
        <label>Firmware / versión<input name="firmware" maxlength="24" placeholder="Si está disponible"></label>
        <button type="submit" id="addAssetBtn">Agregar equipo</button>
      </form><p id="assetFeedback" class="muted small">El inventario permanente se guarda en LittleFS; el historial y la señal al cargar se guardan en la microSD.</p></article>
      <article class="card span12"><div class="row spread"><div><h2>Historial completo de equipos CCTV</h2><p class="muted small">El historial se conserva en la microSD. La fecha y hora se toman del navegador al agregar cada equipo; el RSSI lo mide la ESP32 en ese momento. Los equipos anteriores a esta función aparecen sin fecha ni señal histórica. La descarga incluye solo los registros del día local actual.</p></div><div class="row"><button id="refreshCctvHistoryBtn" class="secondary" type="button">Actualizar tabla</button><a id="downloadCctvHistoryBtn" class="button secondary" href="/api/cctv-history.csv">Descargar CSV de hoy</a></div></div><p id="cctvHistoryStatus" class="muted small">Consultando historial de la microSD…</p><div class="tablewrap"><table><thead><tr><th>Fecha / hora</th><th>IP local</th><th>Nombre</th><th>Tipo</th><th>Fabricante</th><th>Modelo</th><th>MAC</th><th>Firmware</th><th>Wi-Fi al cargar</th></tr></thead><tbody id="cctvHistoryTable"><tr><td colspan="9" class="muted">Consultando historial…</td></tr></tbody></table></div></article>
    </div>
  </section>
  <section class="tabs" id="tab-points">
    <div class="grid">
      <article class="card span12"><div class="row spread"><div><h2>Puntos registrados</h2><p class="muted small">Persistidos en LittleFS. La evaluación se calcula en la ESP32 y compara cada nombre con su medición anterior.</p></div><div class="row"><button id="refreshPointsBtn" class="secondary">Actualizar</button><button id="clearPointsBtn" class="danger">Borrar puntos</button><a class="button secondary" href="/api/points.csv">Descargar CSV</a></div></div><div class="tablewrap"><table><thead><tr><th>Tiempo (s)</th><th>Punto</th><th>Mbps</th><th>Latencia</th><th>RSSI</th><th>Canal</th><th>Score</th><th>Estado y recomendación local</th></tr></thead><tbody id="pointsTable"><tr><td colspan="8" class="muted">Todavía no hay puntos.</td></tr></tbody></table></div></article>
    </div>
  </section>
  <footer class="foot">La identificación del fabricante consulta api.macvendors.com con la MAC del gateway y requiere Internet. La evaluación de cifrado no prueba WPS ni busca vulnerabilidades explotables.</footer>
</div>
<div class="toast" id="toast"></div>
<div id="otaOverlay" class="ota-overlay hidden" role="alertdialog" aria-live="assertive"><div class="ota-box"><h2>Actualización en curso</h2><p id="otaMsg">Preparando actualización…</p><div class="ota-track"><div id="otaBar" class="ota-fill"></div></div><p id="otaPct" class="ota-pct">0%</p><p class="muted small">No reinicies ni apagues el equipo. El portal volverá a funcionar al terminar.</p></div></div>
<script>
const $=id=>document.getElementById(id);
let scanInProgress=false;
let networks=[];
let cctvAssets=[];
let toastTimer=0;
async function copyError(message,button){try{if(navigator.clipboard&&window.isSecureContext){await navigator.clipboard.writeText(message)}else{const field=document.createElement('textarea');field.value=message;field.readOnly=true;field.style.position='fixed';field.style.opacity='0';document.body.append(field);field.focus();field.select();field.setSelectionRange(0,field.value.length);const copied=document.execCommand('copy');field.remove();if(!copied)throw new Error('El navegador no permitió copiar')}button.textContent='Copiado'}catch(error){console.warn('No se pudo copiar el error',error);button.textContent='No se pudo copiar'}}
function toast(message,duration=3200){const el=$('toast');clearTimeout(toastTimer);el.replaceChildren();const text=document.createElement('span');text.textContent=message;el.append(text);if(duration===0){const copy=document.createElement('button');copy.className='secondary';copy.textContent='Copiar error';copy.addEventListener('click',()=>copyError(message,copy));el.append(copy);const close=document.createElement('button');close.className='secondary';close.textContent='Cerrar';close.addEventListener('click',()=>el.classList.remove('show'));el.append(close)}el.classList.add('show');if(duration>0)toastTimer=setTimeout(()=>el.classList.remove('show'),duration)}
async function request(url,options={}){const response=await fetch(url,{credentials:'same-origin',cache:'no-store',...options});if(!response.ok){let msg='Error HTTP '+response.status;try{msg=(await response.json()).error||msg}catch{}const error=new Error(msg);error.status=response.status;throw error}return response}
function parseJsonText(text){try{return JSON.parse(text)}catch(error){const excerpt=text.length>260?text.slice(0,170)+' … '+text.slice(-80):text;throw new Error('JSON inválido ('+text.length+' caracteres): '+error.message+' · '+excerpt)}}
function formBody(values){const body=new URLSearchParams();Object.keys(values).forEach(key=>body.append(key,String(values[key])));return body.toString()}
async function saveDeviceAlias(event){event.preventDefault();const button=$('saveDeviceAliasBtn');button.disabled=true;try{const result=await(await request('/api/device/alias',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody({alias:$('deviceAliasInput').value})})).json();$('deviceAliasFeedback').textContent='Alias '+result.alias+' guardado. El ID técnico del chip no cambia.';toast('Alias del equipo guardado');await refreshStatus()}catch(error){$('deviceAliasFeedback').textContent='No se pudo guardar el alias: '+error.message;toast(error.message,0)}finally{button.disabled=false}}
let otaOverlay=false,otaCurrentVersion='',otaPoll=null;
function showOtaOverlay(pct,msg){$('otaOverlay').classList.remove('hidden');document.body.style.overflow='hidden';$('otaBar').style.width=pct+'%';$('otaPct').textContent=pct+'%';if(msg)$('otaMsg').textContent=msg;window.scrollTo(0,0);otaOverlay=true;if(!otaPoll)otaPoll=setInterval(loadOtaStatus,1000)}
function hideOtaOverlay(){$('otaOverlay').classList.add('hidden');document.body.style.overflow='';otaOverlay=false;if(otaPoll){clearInterval(otaPoll);otaPoll=null}}
async function loadOtaStatus(){if(scanInProgress&&!otaOverlay)return;try{const data=await(await request('/api/ota/status')).json();if(!otaOverlay)otaCurrentVersion=data.currentVersion;if(data.updating){showOtaOverlay(data.progress||0,'Descargando firmware… no reinicies ni apagues el equipo')}else if(otaOverlay){if(data.currentVersion!==otaCurrentVersion){showOtaOverlay(100,'Actualización completada. Recargando…');setTimeout(()=>location.reload(),2000);return}if(!data.updateRequested)hideOtaOverlay()}const keepY=window.scrollY;if(document.activeElement!==$('otaManifestUrl'))$('otaManifestUrl').value=data.manifestUrl||'';$('otaVersion').textContent='Firmware actual: '+data.currentVersion+(data.latestVersion?' · versión publicada: '+data.latestVersion:'');$('otaStatus').textContent=data.status;const updateButton=$('otaUpdateBtn');updateButton.dataset.version=data.availableVersion||'';updateButton.disabled=!data.updateAvailable||data.updating||data.updateRequested;$('otaAvailability').textContent=data.updateAvailable?'Nueva versión disponible: '+data.availableVersion:data.status;if(window.scrollY!==keepY&&!otaOverlay)window.scrollTo(0,keepY)}catch(error){if(otaOverlay){$('otaMsg').textContent='Actualizando o reiniciando… no apagues el equipo. Reconectando con el portal.';return}$('otaUpdateBtn').disabled=true;$('otaAvailability').textContent='No se pudo comprobar si hay una actualización.';$('otaStatus').textContent='No se pudo consultar OTA: '+error.message}}
async function configureOta(event){event.preventDefault();const button=$('otaSaveBtn');button.disabled=true;try{await request('/api/ota/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody({url:$('otaManifestUrl').value})});toast($('otaManifestUrl').value.trim()?'Manifiesto guardado; comprobando actualización':'Actualizaciones automáticas desactivadas');await loadOtaStatus()}catch(error){$('otaStatus').textContent='No se pudo guardar la configuración OTA: '+error.message;toast(error.message,0)}finally{button.disabled=false}}
async function checkOtaNow(){const button=$('otaCheckBtn');button.disabled=true;try{await request('/api/ota/check',{method:'POST'});$('otaStatus').textContent='Comprobación solicitada; espera la respuesta de la ESP32.'}catch(error){$('otaStatus').textContent='No se pudo comprobar OTA: '+error.message;toast(error.message,0)}finally{button.disabled=false}}
async function requestOtaUpdate(){const button=$('otaUpdateBtn');if(button.disabled)return;const version=button.dataset.version;if(!version||!window.confirm('Se descargará e instalará el firmware '+version+'. La ESP32 se reiniciará. ¿Deseas continuar?'))return;button.disabled=true;try{await request('/api/ota/update',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody({confirm:'ACTUALIZAR'})});$('otaAvailability').textContent='Actualización confirmada; preparando descarga…';showOtaOverlay(0,'Preparando actualización… no reinicies ni apagues el equipo');await loadOtaStatus()}catch(error){$('otaAvailability').textContent='No se pudo iniciar la actualización: '+error.message;toast(error.message,0);await loadOtaStatus()}}
async function reportClientError(message){try{await request('/api/client-error',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody({message})});await loadErrorLog()}catch(error){console.warn('No se pudo enviar el error al log de la ESP32',error)}}
async function loadErrorLog(){if(scanInProgress)return;const summary=$('errorLogSummary'),output=$('errorLogContent');try{const content=(await(await request('/api/error-log')).text()).trim(),empty=!content||content==='Sin errores registrados.';const next=empty?'Sin errores registrados.':content;const keepY=window.scrollY;if(output.textContent!==next){const top=output.scrollTop;output.textContent=next;output.scrollTop=top}const count=empty?0:content.split(/\r?\n/).filter(line=>line.startsWith('[boot ')).length;summary.textContent=content.startsWith('LittleFS no disponible;')?'LittleFS no está disponible; los errores mostrados son temporales.':count===1?'1 error registrado localmente.':count+' errores registrados localmente.';if(window.scrollY!==keepY)window.scrollTo(0,keepY)}catch(error){summary.textContent='No se pudo consultar el log.';output.textContent='Error: '+error.message}}
function setSignalDisplay(element,rssi){
 const value=Number(rssi);let quality,className;
 if(!Number.isFinite(value)||value>=0){element.textContent='--';className='signal-unknown'}
 else if(value>=-60){quality='Buena';className='signal-good'}
 else if(value>=-70){quality='Media';className='signal-medium'}
 else{quality='Mala';className='signal-poor'}
 if(Number.isFinite(value)&&value<0)element.textContent=value+' dBm · '+quality;
 element.classList.remove('signal-good','signal-medium','signal-poor','signal-unknown');
 element.classList.add(className);
}
function formatStorageSize(bytes){const value=Math.max(0,Number(bytes)||0);if(value>=1073741824)return (value/1073741824).toFixed(2)+' GB';if(value>=1048576)return (value/1048576).toFixed(2)+' MB';if(value>=1024)return (value/1024).toFixed(1)+' KB';return Math.round(value)+' bytes'}
function renderStorageBar(summaryId,barId,ready,totalBytes,usedBytes){
 const summary=$(summaryId),bar=$(barId),total=Number(totalBytes)||0,used=Math.min(total,Math.max(0,Number(usedBytes)||0));
 bar.replaceChildren();
 if(!ready||total<=0){summary.textContent='No disponible';bar.setAttribute('aria-valuenow','0');bar.setAttribute('aria-valuetext','Almacenamiento no disponible');return}
 const free=Math.max(0,total-used),usedPercent=Math.min(100,used/total*100);
 summary.textContent='Usado: '+formatStorageSize(used)+' · Libre: '+formatStorageSize(free)+' · Total: '+formatStorageSize(total);
 const usedPart=document.createElement('span');usedPart.className='storage-used';usedPart.style.flex='0 0 '+usedPercent+'%';
 const freePart=document.createElement('span');freePart.className='storage-free';freePart.style.flex='1 1 '+(100-usedPercent)+'%';
 bar.append(usedPart,freePart);bar.setAttribute('aria-valuenow',String(Math.round(usedPercent)));bar.setAttribute('aria-valuetext',formatStorageSize(used)+' usados, '+formatStorageSize(free)+' libres');
}
function updateStorageStatus(data){
 renderStorageBar('espStorageSummary','espStorageBar',data.filesystemReady,data.filesystemTotalBytes,data.filesystemUsedBytes);
 renderStorageBar('sdStorageSummary','sdStorageBar',data.sdReady,data.sdTotalBytes,data.sdUsedBytes);
 const status=$('storageStatus');
 status.textContent=data.sdReady?'microSD montada':'microSD no montada: '+data.sdMessage;
 const formatButton=$('formatSdBtn'),formatFeedback=$('sdFormatFeedback'),formatProgress=$('sdFormatProgress');
 formatButton.disabled=data.sdFormatRunning;
 formatProgress.classList.toggle('hidden',!data.sdFormatRunning);
 formatFeedback.textContent=data.sdFormatRunning?'Formato en curso; no retires la tarjeta.':data.sdFormatAttempted?(data.sdFormatSucceeded?'Formato completado: '+data.sdMessage:'Formato no completado: '+data.sdMessage):'El formato borra todos los archivos de la microSD.';
}
async function formatSdCard(){
 if(!confirm('Se borrarán todos los archivos de la microSD. ¿Continuar?'))return;
 const button=$('formatSdBtn');button.disabled=true;
 try{
  const result=await(await request('/api/sd/format',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody({confirm:'FORMATEAR'})})).json();
  if(!result||result.ok!==true)throw new Error('El ESP32 no confirmó el inicio del formato');
  $('sdFormatFeedback').textContent='Formato en curso; no retires la tarjeta.';
  $('sdFormatProgress').classList.remove('hidden');
  toast('Formato de microSD iniciado');
  await refreshStatus();
 }catch(error){
  const detail='No se pudo iniciar el formato de la microSD: '+error.message;
  toast(detail,0);
  await refreshStatus();
 }
}
async function refreshStatus(){
 if(scanInProgress)return;
 try{
  const data=await(await request('/api/status')).json();
  const keepY=window.scrollY,keepX=window.scrollX;
  const device=data.device||{};
  $('deviceId').textContent=device.id||'No disponible';
  $('deviceAliasDisplay').textContent=device.alias||'Sin asignar';
  $('deviceFirmware').textContent=device.firmwareVersion||'--';
  $('deviceHardware').textContent=(device.chipModel||'Chip desconocido')+' rev '+(device.chipRevision??'--')+' · '+(device.chipCores??'--')+' núcleos';
  $('deviceMemory').textContent=(Number(device.flashBytes)/1048576).toFixed(1)+' MB flash · '+(Number(device.psramBytes)/1048576).toFixed(1)+' MB PSRAM';
  $('deviceBootInfo').textContent='#'+(device.bootCount??'--')+' · '+(device.resetReason||'reset desconocido')+' ('+(device.resetReasonCode??'?')+') · ID '+(device.bootId||'--');
  if(document.activeElement!==$('deviceAliasInput'))$('deviceAliasInput').value=device.alias||'';
  $('deviceAliasFeedback').textContent=device.alias?'Alias actual '+device.alias+'. El ID técnico no cambia.':'Asigna CCTV-0001 a esta primera placa. El ID técnico seguirá siendo el identificador de hardware.';
  $('apName').textContent=data.apSsid;
  $('apIp').textContent=data.apIp;
  $('staName').textContent=data.staConnected?data.staSsid:'Sin conexión';
  setSignalDisplay($('staRssi'),data.staConnected?data.rssi:null);
  $('staIp').textContent=data.staConnected?'http://'+data.staIp:'Sin conexión';
  $('gatewayManufacturer').textContent=data.staGatewayManufacturer|| (data.staGatewayMac?'Pendiente':'--');
  $('gatewayMacAddress').textContent='MAC: '+(data.staGatewayMac||'--');
  $('gatewayMacLookupStatus').textContent=data.staConnected?(data.staGatewayMac?data.staGatewayManufacturerStatus:'Esperando resolver la MAC por ARP'):'Sin conexión STA';
  const gatewayLookupLink=$('gatewayMacLookupLink');
  gatewayLookupLink.hidden=!data.staGatewayLookupUrl;
  if(data.staGatewayLookupUrl)gatewayLookupLink.href=data.staGatewayLookupUrl;
  $('statusMessage').textContent=data.message+' · Clientes AP: '+data.apClients;
  $('networkError').textContent=data.error||(!data.filesystemReady?'LittleFS no disponible; no se podrán guardar puntos.':'');
  $('bleStatus').textContent=data.bleProvisioningActive?'BLE activo. En la app ESP BLE Provisioning selecciona '+data.bleServiceName+' e introduce el PIN que aparece en la pantalla.':'BLE no activo. Se inicia automáticamente cuando no hay redes guardadas.';
  $('channelAdvice').textContent=data.channelAdvice;
  if($('assetGateway'))$('assetGateway').textContent=data.staConnected?data.staGateway+(data.staGatewayMac?' · '+data.staGatewayMac:' · buscando MAC por ARP'):'Sin conexión';
  updateStorageStatus(data);
  const pill=$('connectionPill');
  pill.textContent=data.staConnected?'STA conectada':'Sin STA';
  pill.className='pill '+(data.staConnected?'ok':'warn');
  renderWhitelist(data.whitelist||[]);
  if(window.scrollY!==keepY||window.scrollX!==keepX)window.scrollTo(keepX,keepY);
 }catch(error){
  console.warn('No se pudo actualizar el estado del sistema',error);
  $('statusMessage').textContent='No se pudo consultar el estado del ESP32.';
 }
}
function renderWhitelist(list){const root=$('whitelist');root.replaceChildren();if(!list.length){root.textContent='Sin redes guardadas.';return}for(const ssid of list){const item=document.createElement('div');item.className='row';const dot=document.createElement('span');dot.textContent='●';dot.style.color='var(--green)';const name=document.createElement('span');name.textContent=ssid;item.append(dot,name);root.append(item)}}
function setRows(target,rows,empty,render){const root=$(target);root.replaceChildren();if(!rows.length){const tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=target==='pointsTable'?8:6;td.className='muted';td.textContent=empty;tr.append(td);root.append(tr);return}for(const row of rows){const tr=document.createElement('tr');for(const value of render(row)){const td=document.createElement('td');if(value&&typeof value==='object'){td.textContent=value.text;td.className=value.className||''}else td.textContent=value;tr.append(td)}root.append(tr)}}
async function scan(){
 scanInProgress=true;
 const buttons=[$('scanBtn'),$('scanBtnOverview'),$('scanLanNetworksBtn')].filter(Boolean);
 buttons.forEach(button=>{button.disabled=true;button.textContent='Escaneando…'});
 $('scanFeedback').textContent='Escaneando pasivamente redes 2.4 GHz…';
 if($('lanNetworksFeedback'))$('lanNetworksFeedback').textContent='Escaneando pasivamente redes 2.4 GHz…';
 try{
  const result=await(await request('/api/scan',{method:'POST'})).json();
  networks=result;renderScanResults();
  const message='Escaneo completo: '+result.length+' redes encontradas.';
  $('scanFeedback').textContent=message;
  if($('lanNetworksFeedback'))$('lanNetworksFeedback').textContent=message+' Incluye SSID anunciados; no identifica VLAN.';
  toast('Escaneo pasivo: '+result.length+' redes');
 }catch(e){
  const message='No se pudo completar el escaneo: '+e.message;
  $('scanFeedback').textContent=message;
  if($('lanNetworksFeedback'))$('lanNetworksFeedback').textContent=message;
  toast(e.message);
 }finally{
   scanInProgress=false;
  buttons.forEach(button=>{button.disabled=false;button.textContent=button.id==='scanLanNetworksBtn'?'Buscar redes Wi-Fi':'Escanear redes'});
 }
}
function renderLanNetworks(){
 const root=$('lanNetworksTable');
 if(!root)return;
 root.replaceChildren();
 if(!networks.length){
  const row=document.createElement('tr'),cell=document.createElement('td');
  cell.colSpan=6;cell.className='muted';cell.textContent='No se encontraron SSID visibles.';
  row.append(cell);root.append(row);return;
 }
 for(const network of networks){
  const row=document.createElement('tr');
  const identity=network.ssid?'SSID anunciado; puede ser principal, invitado o virtual':'SSID oculto; nombre no anunciado';
  for(const value of [network.ssid||'(SSID oculto)',network.bssid,String(network.channel),network.rssi+' dBm',network.encryption,identity]){
   const cell=document.createElement('td');cell.textContent=value;row.append(cell);
  }
  root.append(row);
 }
}
function renderScanResults(){setRows('scanTable',networks,'No se encontraron redes.',n=>[n.ssid||'(SSID oculto)',n.bssid,n.channel,n.rssi+' dBm',n.encryption,{text:n.risk,className:n.risk.includes('Vulnerable')?'risk-high':n.risk.includes('Protegido')?'risk-protected':'risk-standard'}]);const select=$('networkSelect');select.replaceChildren();const blank=document.createElement('option');blank.value='';blank.textContent=networks.length?'Selecciona una red':'No hay redes disponibles';select.append(blank);for(const n of networks){if(!n.ssid)continue;const option=document.createElement('option');option.value=n.ssid;option.textContent=n.ssid+' · ch '+n.channel+' · '+n.rssi+' dBm';select.append(option)}drawSpectrum();renderLanNetworks()}
const spectrumColors=['#5de1e6','#ff8a75','#9c8cff','#73e0ad','#ffc66d','#ff79b0','#78a9ff','#c6e36a','#e99bff','#7de0ce','#ffbd69','#a6b6ff'];
function drawSpectrum(){
 const canvas=$('spectrum'),ctx=canvas.getContext('2d'),w=canvas.width,h=canvas.height;
 ctx.clearRect(0,0,w,h);ctx.fillStyle='#0d1727';ctx.fillRect(0,0,w,h);
 const left=58,right=w-20,top=22,bottom=h-42;
 ctx.font='12px system-ui';ctx.lineWidth=1;
 for(let db=-30;db>=-100;db-=10){
  const y=top+(-30-db)/70*(bottom-top);
  ctx.strokeStyle='#263650';ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();
  ctx.fillStyle='#9aabc2';ctx.textAlign='right';ctx.fillText(db+' dBm',left-8,y+4);
 }
 for(let ch=1;ch<=13;ch++){
  const x=left+(ch-1)*(right-left)/12;
  ctx.strokeStyle='#1d2b40';ctx.beginPath();ctx.moveTo(x,top);ctx.lineTo(x,bottom);ctx.stroke();
  ctx.fillStyle='#a9b8cb';ctx.textAlign='center';ctx.fillText(String(ch),x,bottom+20);
 }
 ctx.fillStyle='#9aabc2';ctx.textAlign='center';ctx.fillText('Canal Wi-Fi 2.4 GHz',(left+right)/2,h-7);
 const placedLabels=[];
 networks.forEach((n,index)=>{
  if(n.channel<1||n.channel>13)return;
  const color=spectrumColors[index%spectrumColors.length];
  const x=left+(n.channel-1)*(right-left)/12;
  const y=top+Math.max(0,Math.min(70,(-30-n.rssi)))/70*(bottom-top);
  ctx.fillStyle=color+'30';ctx.strokeStyle=color+'bb';
  ctx.beginPath();ctx.moveTo(Math.max(left,x-3*(right-left)/12),bottom);
  ctx.quadraticCurveTo(x,y,Math.min(right,x+3*(right-left)/12),bottom);
  ctx.closePath();ctx.fill();ctx.stroke();
  ctx.fillStyle=color;ctx.beginPath();ctx.arc(x,y,5,0,Math.PI*2);ctx.fill();

  const ssid=n.ssid||'(oculta)';
  const label=ssid.length>14?ssid.slice(0,13)+'…':ssid;
  ctx.font='11px system-ui';
  const textWidth=ctx.measureText(label).width;
  const labelX=Math.max(left,Math.min(right-textWidth-8,x+8));
  let labelY=Math.max(top+13,y-8);
  for(let attempts=0;attempts<40&&placedLabels.some(p=>Math.abs(p.x-labelX)<Math.max(p.width,textWidth+8)&&Math.abs(p.y-labelY)<14);attempts++){
   labelY+=14;
   if(labelY>bottom-3)labelY=top+13;
  }
  ctx.fillStyle='rgba(8,15,26,.88)';ctx.fillRect(labelX-3,labelY-11,textWidth+8,15);
  ctx.fillStyle=color;ctx.textAlign='left';ctx.fillText(label,labelX,labelY);
  placedLabels.push({x:labelX,y:labelY,width:textWidth+8});
 });
 const legend=$('spectrumLegend');legend.replaceChildren();
 if(!networks.length){legend.textContent='No se encontraron redes.';return}
 networks.forEach((n,index)=>{
  const item=document.createElement('div');item.className='legend-item';
  const swatch=document.createElement('span');swatch.className='legend-swatch';swatch.style.background=spectrumColors[index%spectrumColors.length];
  const text=document.createElement('span');text.className='legend-text';text.textContent=(index+1)+'. '+(n.ssid||'(SSID oculto)')+' · canal '+n.channel+' · '+n.rssi+' dBm';
  item.append(swatch,text);legend.append(item);
 });
}
async function connectNetwork(){const ssid=$('networkSelect').value,password=$('wifiPassword').value;if(!ssid){toast('Selecciona una red escaneada');return}const button=$('connectBtn');button.disabled=true;try{await request('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody({ssid,password})});$('wifiPassword').value='';toast('Intentando conectar; se guardará si tiene éxito');setTimeout(refreshStatus,1000)}catch(e){toast(e.message)}finally{button.disabled=false}}
function renderPoints(rows){setRows('pointsTable',rows,'Todavía no hay puntos.',p=>[(p.uptimeMs/1000).toFixed(1),p.name,Number(p.mbps).toFixed(2),Number(p.latencyMs).toFixed(1)+' ms',p.rssi<0?p.rssi+' dBm':'No disponible',p.channel,p.score+'/100',p.quality+'. '+p.recommendation+(p.anomaly!=='Sin anomalía'?' · '+p.anomaly:'')])}
function configureCctvIpInput(){const manual=$('assetIpSelect').value==='__manual__',input=$('assetIpManual');input.hidden=!manual;input.disabled=!manual;input.required=manual;if(!manual)input.value=''}
async function loadCctvIpOptions(){
 const select=$('assetIpSelect'),previous=select.value,errors=[];
 const fetchJson=async(path,label)=>{try{return await(await request(path)).json()}catch(error){errors.push(label);return null}};
 const [arpData,routerData,historyData]=await Promise.all([fetchJson('/api/arp-neighbors','ARP'),fetchJson('/api/router-bindings','reservas del gateway'),fetchJson('/api/discovery-history','historial de descubrimientos')]);
 cctvIpInfo=new Map();
 const info=ip=>{if(!cctvIpInfo.has(ip))cctvIpInfo.set(ip,{});return cctvIpInfo.get(ip)};
 if(Array.isArray(arpData))for(const neighbor of arpData)if(neighbor.mac)info(neighbor.ip).mac=neighbor.mac;
 if(routerData&&Array.isArray(routerData.bindings))for(const binding of routerData.bindings){const item=info(binding.ip);if(binding.mac)item.mac=binding.mac;if(binding.description)item.name=binding.description}
 if(historyData&&Array.isArray(historyData.records))for(const record of historyData.records.slice().sort((a,b)=>(a.date+a.time).localeCompare(b.date+b.time))){
  const item=info(record.ip),text=[record.identity,record.model,record.deviceClass].join(' ');
  if(record.mac)item.mac=record.mac;
  if(record.model||record.identity)item.model=record.model||record.identity;
  if(record.firmware)item.firmware=record.firmware;
  if(record.source==='SADP')item.manufacturer=/ezviz/i.test(text)?'EZVIZ':'HIKVISION';
  else if(record.source==='DHDiscover')item.manufacturer='DAHUA';
  if(/nvr/i.test(text))item.type='NVR';else if(/dvr|xvr/i.test(text))item.type='DVR';
 }
 const ips=new Map();
 const add=(ip,source,registered=false)=>{
  if(!isValidIpv4(ip))return;
  const entry=ips.get(ip)||{sources:new Set(),registered:false};
  entry.sources.add(source);entry.registered=entry.registered||registered;ips.set(ip,entry);
 };
 for(const asset of cctvAssets)if(asset.ip&&asset.ipMode!=='manual'&&asset.ipMode!=='none')add(asset.ip,'Inventario',true);
 if(Array.isArray(arpData))for(const neighbor of arpData)add(neighbor.ip,'ARP');
 if(routerData&&Array.isArray(routerData.bindings))for(const binding of routerData.bindings)add(binding.ip,'Gateway');
 const sorted=[...ips.entries()].sort(([left],[right])=>{
  const a=left.split('.').map(Number),b=right.split('.').map(Number);
  for(let i=0;i<4;i++)if(a[i]!==b[i])return a[i]-b[i];
  return 0;
 });
 select.replaceChildren();
 const placeholder=document.createElement('option');
 placeholder.value='';placeholder.disabled=true;placeholder.textContent=sorted.length?'Selecciona una IP detectada':'No hay IP detectadas';
 select.append(placeholder);
 for(const [ip,entry] of sorted){
  const option=document.createElement('option');
  option.value=ip;option.disabled=entry.registered;
  option.textContent=ip+' · '+[...entry.sources].join(', ')+(entry.registered?' · ya registrada':'');
  select.append(option);
 }
 const noneOption=document.createElement('option');
 noneOption.value='__none__';noneOption.textContent='Ninguna (registrar sin IP por ahora)';
 select.append(noneOption);
 const manualOption=document.createElement('option');
 manualOption.value='__manual__';manualOption.textContent='Ingresar IP manualmente (temporal, sin cotejo)';
 select.append(manualOption);
 const previousOption=[...select.options].find(option=>option.value===previous&&!option.disabled);
 select.value=previousOption?previous:(sorted.some(([,entry])=>!entry.registered)?'':'__none__');
 configureCctvIpInput();
 const available=sorted.filter(([,entry])=>!entry.registered).length;
 const registered=sorted.length-available;
 let message=available+' IP detectada(s) disponible(s).';
 if(registered)message+=' '+registered+' ya registrada(s).';
 if(errors.length)message+=' No se pudo consultar: '+errors.join(' y ')+'.';
 if(!sorted.length){message='No se encontraron IP; puedes registrar sin IP o ingresar una temporal.';if(errors.length)message+=' No se pudo consultar: '+errors.join(' y ')+'.'}
 $('assetIpFeedback').textContent=message;
}
function localCctvDateTime(){const now=new Date(),pad=value=>String(value).padStart(2,'0');return{date:now.getFullYear()+'-'+pad(now.getMonth()+1)+'-'+pad(now.getDate()),time:pad(now.getHours())+':'+pad(now.getMinutes())+':'+pad(now.getSeconds())}}
function discoveryPostOptions(){const localTime=localCctvDateTime();return{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formBody({recordDate:localTime.date,recordTime:localTime.time})}}
function discoveryPersistenceNotice(result){if(!result.devices.length)return'';if(result.saved===true)return' Guardado en microSD: '+Number(result.savedCount||0)+' registro(s).';return' No se pudo guardar en microSD: '+(result.saveError||'error no especificado')+'.'}
function renderDiscoveryHistory(records){const body=$('discoveryHistoryTable');body.replaceChildren();if(!records.length){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=9;cell.className='muted';cell.textContent='Todavía no hay detecciones guardadas en la microSD.';row.append(cell);body.append(row);return}for(const record of records){const row=document.createElement('tr'),firmwareSerial=[record.firmware,record.serial].filter(Boolean).join(' · '),network=[record.ssid,record.gatewayIp?'Gateway '+record.gatewayIp:''].filter(Boolean).join(' · '),values=[record.date+' '+record.time,record.source,record.ip,record.protocol,record.identity||record.model||'--',record.mac||'--',firmwareSerial||'--',network||'--',record.endpoint||'--'];for(const value of values){const cell=document.createElement('td');cell.textContent=value;row.append(cell)}body.append(row)}}
async function loadDiscoveryHistory(){try{const result=await(await request('/api/discovery-history')).json();if(!result||!Array.isArray(result.records))throw new Error('Formato de historial inválido');renderDiscoveryHistory(result.records);$('discoveryHistoryStatus').textContent=result.records.length+' registros recientes en la microSD'+(result.truncated?' · se muestran los últimos 200':'')+'.';return true}catch(error){const detail='No se pudo leer el historial de descubrimientos de la microSD: '+error.message;$('discoveryHistoryTable').replaceChildren();const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=9;cell.className='muted';cell.textContent=detail;row.append(cell);$('discoveryHistoryTable').append(row);$('discoveryHistoryStatus').textContent=detail;return false}}
function renderCctvHistory(records){const body=$('cctvHistoryTable');body.replaceChildren();if(!records.length){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=9;cell.className='muted';cell.textContent='Todavía no hay equipos en el historial.';row.append(cell);body.append(row);return}for(const record of records.slice().reverse()){const row=document.createElement('tr'),values=[record.date&&record.time?record.date+' '+record.time:'Anterior a este historial',record.ip||'--',record.name||'--',record.type||'--',record.manufacturer||'--',record.model||'--',record.mac||'--',record.firmware||'--'];for(const value of values){const cell=document.createElement('td');cell.textContent=value;row.append(cell)}const signalCell=document.createElement('td'),signal=document.createElement('span');setSignalDisplay(signal,record.rssi);signalCell.append(signal);row.append(signalCell);body.append(row)}}
async function loadCctvHistory(){const {date}=localCctvDateTime();$('downloadCctvHistoryBtn').href='/api/cctv-history.csv?date='+encodeURIComponent(date);try{const records=await(await request('/api/cctv-history')).json();if(!Array.isArray(records))throw new Error('Formato de historial inválido');renderCctvHistory(records);$('cctvHistoryStatus').textContent=records.length+' equipos en el historial de la microSD · CSV del día '+date+'.';return true}catch(error){const detail='No se pudo leer el historial CCTV de la microSD: '+error.message;$('cctvHistoryTable').replaceChildren();const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=9;cell.className='muted';cell.textContent=detail;row.append(cell);$('cctvHistoryTable').append(row);$('cctvHistoryStatus').textContent=detail;return false}}
async function loadCctvAssets(){try{const [assetsResponse,statusResponse]=await Promise.all([request('/api/cctv-assets'),request('/api/status')]);cctvAssets=await assetsResponse.json();const status=await statusResponse.json();$('assetGateway').textContent=status.staConnected?status.staGateway+(status.staGatewayMac?' · '+status.staGatewayMac:' · buscando MAC por ARP'):'Sin conexión';$('assetNetwork').textContent=status.staConnected?status.staSsid:'Sin conexión';await loadCctvIpOptions();$('assetFeedback').textContent=cctvAssets.length+' equipos guardados en el inventario permanente.';await loadCctvHistory();return true}catch(e){const detail='No se pudo cargar el inventario CCTV: '+e.message;$('assetFeedback').textContent=detail;toast(detail,0);return false}}
async function addCctvAsset(){const form=$('assetForm'),button=$('addAssetBtn');button.disabled=true;try{const selection=$('assetIpSelect').value,ipMode=selection==='__manual__'?'manual':selection==='__none__'?'none':'detected',ip=ipMode==='none'?'':ipMode==='manual'?$('assetIpManual').value.trim():selection;if(ipMode!=='none'&&!isValidIpv4(ip))throw new Error('Selecciona una IP detectada o ingresa una IP temporal válida');const body=new URLSearchParams(new FormData(form)),localTime=localCctvDateTime();body.set('ip',ip);body.set('ipMode',ipMode);body.set('recordDate',localTime.date);body.set('recordTime',localTime.time);const response=await request('/api/cctv-assets',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const result=await response.json();if(!result||result.ok!==true)throw new Error('El ESP32 no confirmó el registro');form.reset();configureCctvIpInput();const refreshed=await loadCctvAssets(),signal=result.rssi===null||result.rssi===undefined?'RSSI no disponible':Number(result.rssi)+' dBm';$('assetFeedback').textContent=refreshed?'Equipo agregado. Señal Wi-Fi al cargar: '+signal+'.':'Equipo agregado; no se pudo actualizar la tabla.';toast('Equipo agregado · Wi-Fi '+signal)}catch(e){const detail='No se pudo agregar el equipo: '+e.message;$('assetFeedback').textContent=detail;await loadCctvHistory();toast(detail,0)}finally{button.disabled=false}}
async function runTest(){
 if(!$('pointName').value.trim()){toast('Escribe un nombre para el punto');return}
 const button=$('runTestBtn'),message=$('testMessage');let stage='comprobar la conexión STA';
 button.disabled=true;message.textContent='Midiendo la respuesta del router y la señal Wi-Fi…';
 try{
  stage='medir la respuesta del router';
  try{
   const gateway=await(await request('/api/gateway-ping')).json();
   $('gatewayLatencyResult').textContent=gateway.reachable?Number(gateway.averageMs).toFixed(1)+' ms':'Sin respuesta ICMP';
   $('gatewayLatencyResult').title='Gateway '+gateway.target;
   setSignalDisplay($('signalResult'),gateway.rssi);
   setSignalDisplay($('staRssi'),gateway.rssi);
  }catch(error){
   $('gatewayLatencyResult').textContent='No disponible';
   $('gatewayLatencyResult').title=error.message;
   const status=await(await request('/api/status')).json();
   setSignalDisplay($('signalResult'),status.staConnected?status.rssi:null);
  }
  message.textContent='Router medido; descargando datos de Internet…';
  const bytes=Number($('testSize').value);
  stage='descargar datos desde speed.cloudflare.com';
  stage='descargar datos desde speed.cloudflare.com';
  const result=await(await request('/api/test?bytes='+bytes)).json();
  const mbps=Number(result&&result.downloadMbps),latency=Number(result&&result.latencyMs);
  if(!result||result.ok!==true||!Number.isFinite(mbps)||!Number.isFinite(latency))throw new Error('La ESP32 no devolvió un resultado externo válido');
  $('speedResult').textContent=mbps.toFixed(2)+' Mbps';
  $('latencyResult').textContent=latency.toFixed(0)+' ms';
  stage='guardar el punto en LittleFS';
  message.textContent='Descarga externa completa; guardando punto…';
  const body=formBody({name:$('pointName').value,mbps:mbps.toFixed(3),latencyMs:latency.toFixed(2)});
  const recordResponse=await request('/api/record',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  stage='confirmar el guardado';
  const saved=parseJsonText(await recordResponse.text());
  if(!saved||saved.ok!==true)throw new Error('El ESP32 no confirmó el guardado del punto');
  message.textContent='Punto guardado · router '+$('gatewayLatencyResult').textContent+' · señal '+$('signalResult').textContent+' · '+(Number(result.bytesReceived)/1048576).toFixed(1)+' MB desde '+result.server+' en '+result.transferMs+' ms';
  toast('Prueba externa registrada');
  stage='actualizar la lista de puntos';
  if(!await loadPoints())message.textContent='Punto guardado, pero no se pudo actualizar la lista; revisa el mensaje de error.';
 }catch(e){
  const detail='Falló al '+stage+': '+e.message;
  message.textContent=detail;reportClientError(detail);toast(detail,0);
 }finally{button.disabled=false}
}
async function loadPoints(){try{renderPoints(parseJsonText(await(await request('/api/points')).text()));return true}catch(e){const detail='No se pudo cargar Puntos guardados: '+e.message;reportClientError(detail);toast(detail,0);return false}}
async function clearPoints(){if(!confirm('¿Borrar todos los puntos guardados? Esta acción no se puede deshacer.'))return;const button=$('clearPointsBtn');button.disabled=true;try{const response=await request('/api/points',{method:'DELETE'});const result=await response.json();if(!result||result.ok!==true)throw new Error('El ESP32 no confirmó el borrado');renderPoints([]);toast('Se borraron los puntos guardados')}catch(e){const detail='No se pudieron borrar los puntos: '+e.message;reportClientError(detail);toast(detail,0)}finally{button.disabled=false}}
function renderSadpResults(devices){const root=$('sadpTable');root.replaceChildren();if(!devices.length){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=5;cell.className='muted';cell.textContent='No hubo respuestas SADP compatibles en el tiempo de escucha.';row.append(cell);root.append(row);return}for(const device of devices){const row=document.createElement('tr');for(const value of [device.ip,device.mac||'No informada',device.model||'No informado',device.firmware||'No informada']){const cell=document.createElement('td');cell.textContent=value;row.append(cell)}row.append(htmlCell(device.ip));root.append(row)}}
async function discoverSadpCameras(){const button=$('sadpDiscoverBtn'),feedback=$('sadpFeedback');button.disabled=true;feedback.textContent='Enviando consulta multicast SADP y escuchando respuestas durante 3 segundos…';try{const result=await(await request('/api/sadp/discover',discoveryPostOptions())).json();if(!result||result.ok!==true||!Array.isArray(result.devices))throw new Error('La ESP32 no devolvió resultados SADP válidos');renderSadpResults(result.devices);feedback.textContent=result.devices.length?result.devices.length+' dispositivo(s) SADP encontrado(s). Revisa las IP y datos antes de agregarlos al inventario.'+discoveryPersistenceNotice(result):'No se recibieron respuestas compatibles; revisa que los equipos estén en la misma LAN y que el router permita multicast.';if(result.devices.length&&result.saved!==true)toast('Dispositivos encontrados, pero el historial no se guardó en la microSD: '+(result.saveError||'error no especificado'),0);await loadDiscoveryHistory()}catch(error){const detail='No se pudo completar el descubrimiento SADP: '+error.message;feedback.textContent=detail;renderSadpResults([]);toast(detail,0)}finally{button.disabled=false}}
function renderDhDiscoverResults(devices){const root=$('dhDiscoverTable');root.replaceChildren();if(!devices.length){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=7;cell.className='muted';cell.textContent='No hubo respuestas DHDiscover compatibles en el tiempo de escucha.';row.append(cell);root.append(row);return}for(const device of devices){const row=document.createElement('tr');for(const value of [device.ip,device.mac||'No informada',device.model||'No informado',device.deviceClass||'No informada',device.firmware||'No informada',device.serial||'No informado']){const cell=document.createElement('td');cell.textContent=value;row.append(cell)}row.append(htmlCell(device.ip));root.append(row)}}
async function discoverDahuaDevices(){const button=$('dhDiscoverBtn'),feedback=$('dhDiscoverFeedback');button.disabled=true;feedback.textContent='Enviando consulta DHDiscover multicast y escuchando respuestas durante 3 segundos…';try{const result=await(await request('/api/dhdiscover/discover',discoveryPostOptions())).json();if(!result||result.ok!==true||!Array.isArray(result.devices))throw new Error('La ESP32 no devolvió resultados DHDiscover válidos');renderDhDiscoverResults(result.devices);feedback.textContent=result.devices.length?result.devices.length+' dispositivo(s) Dahua encontrado(s). Revisa los datos antes de agregarlos al inventario.'+discoveryPersistenceNotice(result):'No se recibieron respuestas compatibles; revisa que los equipos Dahua estén en la misma LAN y que el router permita multicast.';if(result.devices.length&&result.saved!==true)toast('Dispositivos encontrados, pero el historial no se guardó en la microSD: '+(result.saveError||'error no especificado'),0);await loadDiscoveryHistory()}catch(error){const detail='No se pudo completar el descubrimiento DHDiscover: '+error.message;feedback.textContent=detail;renderDhDiscoverResults([]);toast(detail,0)}finally{button.disabled=false}}
function renderGenericDiscoveryResults(devices){const root=$('genericDiscoverTable');root.replaceChildren();if(!devices.length){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=5;cell.className='muted';cell.textContent='No hubo respuestas compatibles con ONVIF, SSDP o mDNS.';row.append(cell);root.append(row);return}for(const device of devices){const row=document.createElement('tr');for(const value of [device.ip,device.protocol,device.identity||'Sin identificación',device.endpoint||'No anunciado']){const cell=document.createElement('td');cell.textContent=value;row.append(cell)}row.append(htmlCell(device.ip));root.append(row)}}
async function discoverGenericDevices(){const button=$('genericDiscoverBtn'),feedback=$('genericDiscoverFeedback');button.disabled=true;feedback.textContent='Consultando ONVIF/WS-Discovery, SSDP/UPnP y mDNS/DNS-SD; puede tardar hasta 6 segundos…';try{const result=await(await request('/api/generic-discovery',discoveryPostOptions())).json();if(!result||result.ok!==true||!Array.isArray(result.devices))throw new Error('La ESP32 no devolvió resultados genéricos válidos');renderGenericDiscoveryResults(result.devices);feedback.textContent=result.devices.length?result.devices.length+' dispositivo(s)/servicio(s) encontrado(s). No se enviaron consultas SADP ni DHDiscover.'+discoveryPersistenceNotice(result):'No se recibieron respuestas genéricas compatibles; los equipos pueden no anunciar estos protocolos o el router puede filtrar multicast.';if(result.devices.length&&result.saved!==true)toast('Dispositivos encontrados, pero el historial no se guardó en la microSD: '+(result.saveError||'error no especificado'),0);await loadDiscoveryHistory()}catch(error){const detail='No se pudo completar la búsqueda genérica: '+error.message;feedback.textContent=detail;renderGenericDiscoveryResults([]);toast(detail,0)}finally{button.disabled=false}}
function selectTab(name){document.querySelectorAll('.tabs').forEach(el=>el.classList.toggle('active',el.id==='tab-'+name));document.querySelectorAll('.nav button').forEach(el=>el.classList.toggle('active',el.dataset.tab===name));if(name==='points')loadPoints();if(name==='devices'){loadLanDevices();loadDiscoveryHistory()}if(name==='cctv')loadCctvAssets();if(name==='overview')loadErrorLog()}
document.querySelectorAll('.nav button').forEach(button=>button.addEventListener('click',()=>selectTab(button.dataset.tab)));
$('gatewayPingBtn').addEventListener('click',async()=>{const button=$('gatewayPingBtn'),result=$('gatewayPingResult');button.disabled=true;result.textContent='Enviando un ping ICMP…';try{const data=await(await request('/api/gateway-ping')).json();result.textContent=data.reachable?'Gateway '+data.target+' respondió · '+Number(data.averageMs).toFixed(1)+' ms':'Gateway '+data.target+' no respondió al ping ICMP.'}catch(error){result.textContent='No se pudo probar el gateway: '+error.message}finally{button.disabled=false}});
$('sadpDiscoverBtn').addEventListener('click',discoverSadpCameras);
$('dhDiscoverBtn').addEventListener('click',discoverDahuaDevices);
$('genericDiscoverBtn').addEventListener('click',discoverGenericDevices);
function htmlCell(ip){const cell=document.createElement('td');cell.dataset.htmlIp=ip;cell.className='muted';cell.textContent='Sin comprobar';return cell}
let discoveredHtmlBusy=false;
async function checkDiscoveredHtml(button){
 if(discoveredHtmlBusy||activeHttpCheck||activeDevicePing)return;
 const root=$(button.dataset.htmlCheck),feedback=button.closest('article').querySelector('p[id$="Feedback"]');
 const cells=[...root.querySelectorAll('td[data-html-ip]')];
 if(!cells.length){feedback.textContent='Primero busca dispositivos y luego comprueba HTML.';return}
 if(!lanStaConnected){feedback.textContent='Conecta la STA a la LAN antes de comprobar los servidores HTML.';return}
 discoveredHtmlBusy=true;button.disabled=true;
 const byIp=new Map();for(const cell of cells){if(!byIp.has(cell.dataset.htmlIp))byIp.set(cell.dataset.htmlIp,[]);byIp.get(cell.dataset.htmlIp).push(cell);cell.className='muted';cell.textContent='Pendiente'}
 let done=0,limit='';const ips=[...byIp.keys()].slice(0,32);
 try{
  for(const ip of ips){
   const targets=byIp.get(ip),links=[],notes=[];
   for(const port of [80,8080]){
    targets.forEach(cell=>{cell.textContent='Comprobando puerto '+port+'…'});
    try{
     const data=await(await request('/api/http-server-check?ip='+encodeURIComponent(ip)+'&port='+port)).json();
     if(data.html)links.push([port]);else notes.push(port+': '+(data.status?'HTTP '+data.status:data.connected?'sin HTML':'sin respuesta'));
    }catch(error){
     notes.push(port+': no comprobado');
     if(error.message.includes('límite')||error.message.includes('Espera')||error.message.includes('Conecta la STA')){limit=error.message;break}
    }
    await new Promise(resolve=>setTimeout(resolve,1200));
   }
   for(const cell of targets){
    cell.replaceChildren();cell.className=links.length?'':'muted';
    links.forEach(([port],index)=>{if(index)cell.append(document.createTextNode(' · '));cell.append(createHttpLink(ip,'Abrir :'+port,port))});
    if(!links.length)cell.textContent=notes.join(' · ');
   }
   done++;feedback.textContent=done+' de '+ips.length+' IP comprobadas (HTTP 80 y 8080).';
   if(limit)break;
  }
  feedback.textContent=limit?'Comprobación detenida: '+limit:done+' IP comprobadas. Solo se enlazan respuestas HTML.'+(byIp.size>ips.length?' Se omitieron '+(byIp.size-ips.length)+' IP por el límite de 32.':'');
 }finally{discoveredHtmlBusy=false;button.disabled=false}
}
document.querySelectorAll('[data-html-check]').forEach(button=>button.addEventListener('click',()=>checkDiscoveredHtml(button)));
$('scanBtn').addEventListener('click',scan);$('scanBtnOverview').addEventListener('click',scan);$('connectBtn').addEventListener('click',connectNetwork);$('formatSdBtn').addEventListener('click',formatSdCard);$('runTestBtn').addEventListener('click',runTest);$('refreshPointsBtn').addEventListener('click',loadPoints);$('clearPointsBtn').addEventListener('click',clearPoints);$('assetForm').addEventListener('submit',e=>{e.preventDefault();addCctvAsset()});let cctvIpInfo=new Map();
function fillCctvFromIp(){
 const info=cctvIpInfo.get($('assetIpSelect').value),form=$('assetForm');
 const fields={name:'name',type:'type',mac:'mac',manufacturer:'manufacturer',model:'model',firmware:'firmware'};
 for(const key of Object.keys(fields)){
  const field=form.elements[key];if(!field)continue;
  if(field.dataset.auto==='1'&&(!info||!info[key])){field.value=field.tagName==='SELECT'&&key==='manufacturer'?'':field.value;if(field.tagName!=='SELECT')field.value='';delete field.dataset.auto}
  if(!info||!info[key])continue;
  const empty=!field.value||field.dataset.auto==='1';
  if(!empty)continue;
  let value=String(info[key]);
  if(key==='mac'){const hex=value.replace(/[^0-9a-fA-F]/g,'').toUpperCase();if(hex.length===12)value=hex.match(/.{2}/g).join(':')}
  if(field.maxLength>0)value=value.slice(0,field.maxLength);
  if(field.tagName==='SELECT'&&![...field.options].some(option=>option.value===value))continue;
  field.value=value;field.dataset.auto='1';
 }
}
['name','type','mac','manufacturer','model','firmware'].forEach(key=>{const field=$('assetForm').elements[key];if(field)field.addEventListener('input',()=>{delete field.dataset.auto})});
$('assetIpSelect').addEventListener('change',fillCctvFromIp);
$('assetIpSelect').addEventListener('change',configureCctvIpInput);
$('refreshCctvHistoryBtn').addEventListener('click',loadCctvHistory);
$('refreshDiscoveryHistoryBtn').addEventListener('click',loadDiscoveryHistory);
$('scanLanNetworksBtn').addEventListener('click',scan);
$('refreshErrorLogBtn').addEventListener('click',loadErrorLog);
$('deviceAliasForm').addEventListener('submit',saveDeviceAlias);
$('otaConfigForm').addEventListener('submit',configureOta);$('otaCheckBtn').addEventListener('click',checkOtaNow);$('otaUpdateBtn').addEventListener('click',requestOtaUpdate);
refreshStatus();loadErrorLog();loadOtaStatus();setInterval(refreshStatus,5000);setInterval(loadErrorLog,30000);setInterval(loadOtaStatus,10000);
</script>
<script>
let activeDevicePing=null;
let activeHttpCheck=null;
let httpCheckEntries=[];
let lanStaConnected=false;
const httpCheckCells=new Map();
const httpCheckCellResults=new Map();
const httpCheckFeedback=document.getElementById('httpCheckFeedback');
function isValidIpv4(ip){
 const parts=String(ip).split('.');
 return parts.length===4&&parts.every(part=>/^(0|[1-9]\d{0,2})$/.test(part)&&Number(part)<=255);
}
if(httpCheckFeedback)httpCheckFeedback.textContent='Al pulsar, se carga la tabla del router que coincida con el gateway actual y se solicita GET / en 80/8080 solo para esas IP conocidas. Máximo 32 direcciones; no se barre el rango.';
const lanDevicesTable=document.getElementById('lanDevicesTable');
if(lanDevicesTable){
 const lanArticle=lanDevicesTable.closest('article'),headers=lanArticle.querySelectorAll('table thead th'),paragraphs=lanArticle.querySelectorAll('p');
 if(paragraphs[0])paragraphs[0].textContent='Combina vecinos observados por ARP, equipos importados y la tabla guardada que coincide con el gateway actual. No barre la red.';
 if(headers[6])headers[6].textContent='Servidores HTML';
 if(paragraphs[3])paragraphs[3].textContent='“Abrir HTTP” aparece si una IP ARP, importada o de la tabla del gateway responde en 80 o 8080 con Content-Type HTML o si la respuesta contiene estructura HTML. Las reservas no confirman que el equipo esté conectado; usa Ping por fila para comprobarlo. No se recorren rangos.';
}
const httpResultsTable=document.getElementById('httpCheckResultsTable');
if(httpResultsTable){
 const resultsDescription=httpResultsTable.closest('article').querySelector('p');
 if(resultsDescription)resultsDescription.textContent='Resultados de dispositivos observados por ARP, importados o de la tabla guardada del gateway actual. Se solicita GET / solo a los puertos 80 y 8080; máximo 32 direcciones por ejecución.';
}
async function loadLanDevices(){
 const button=document.getElementById('refreshDevicesBtn'),root=document.getElementById('lanDevicesTable'),feedback=document.getElementById('lanDevicesFeedback');
 if(activeDevicePing||activeHttpCheck){feedback.textContent='Finaliza la comprobación actual antes de actualizar la tabla.';return false}
 button.disabled=true;feedback.textContent='Cargando observaciones ARP e inventario…';
 try{
  const errors=[],fetchJson=(path,label)=>request(path).then(response=>response.json()).catch(error=>{errors.push(label+': '+error.message);return null});
  const [arpData,assetData,status,routerData]=await Promise.all([fetchJson('/api/arp-neighbors','ARP'),fetchJson('/api/cctv-assets','Inventario'),fetchJson('/api/status','Estado de red'),fetchJson('/api/router-bindings','Tabla del gateway')]);
  const arpLoaded=Array.isArray(arpData),assetsLoaded=Array.isArray(assetData),routerLoaded=Boolean(routerData&&Array.isArray(routerData.bindings));
  const arpRows=Array.isArray(arpData)?arpData:[],assets=Array.isArray(assetData)?assetData:[],comparableAssets=assets.filter(asset=>asset&&asset.ip&&asset.ipMode!=='manual'&&asset.ipMode!=='none'),routerRows=routerLoaded?routerData.bindings:[],network=status&&status.staConnected?status.staSsid:'Sin conexión STA',devices=new Map();
  lanStaConnected=Boolean(status&&status.staConnected);
  httpCheckEntries=[...new Map([...arpRows,...comparableAssets,...routerRows].filter(entry=>entry&&entry.ip&&isValidIpv4(entry.ip)).map(entry=>[entry.ip,{ip:entry.ip}])).values()];
  httpCheckCells.clear();
  httpCheckCellResults.clear();
  for(const asset of comparableAssets)devices.set(asset.ip,{ip:asset.ip,network:asset.networkName||network,mac:asset.mac,name:asset.name,state:'En inventario; actividad no confirmada',sources:new Set(['Inventario CCTV']),arpObserved:false,inventoryImported:true});
  for(const binding of routerRows){
   const device=devices.get(binding.ip)||{ip:binding.ip,network,name:binding.description||'Sin identificar',mac:binding.mac||'',state:'En tabla del router; actividad no confirmada',sources:new Set(),arpObserved:false,inventoryImported:false,routerBinding:true};
   if(!device.sources)device.sources=new Set();
   device.sources.add('Reserva del gateway');
   device.name=device.name||binding.description||'Sin identificar';device.mac=device.mac||binding.mac||'';
   device.state=binding.arpObserved?'ARP observado hace '+binding.lastSeenSeconds+' s; no confirma actividad actual':'Solo reserva del gateway; conexión no confirmada';
   device.routerBinding=true;devices.set(binding.ip,device);
  }
  for(const neighbor of arpRows){
   const device=devices.get(neighbor.ip)||{ip:neighbor.ip,network,name:'Sin identificar',mac:'',state:'',sources:new Set(),arpObserved:false,inventoryImported:false};
   if(!device.sources)device.sources=new Set();
   device.sources.add('ARP');
   const macConflict=device.mac&&neighbor.mac&&String(device.mac).replace(/-/g,':').toUpperCase()!==String(neighbor.mac).replace(/-/g,':').toUpperCase();
   device.network=device.network||network;device.mac=neighbor.mac||device.mac;
   device.state='ARP observado hace '+neighbor.lastSeenSeconds+' s'+(macConflict?'; MAC distinta del inventario o reserva':'')+'; actividad actual no confirmada';
   device.arpObserved=true;devices.set(neighbor.ip,device);
  }
  root.replaceChildren();
  if(!devices.size){const row=document.createElement('tr'),cell=document.createElement('td');cell.colSpan=8;cell.className='muted';cell.textContent=errors.length?'No se pudieron cargar datos: '+errors.join(' · '):'Sin observaciones ARP, reservas del gateway ni equipos en el inventario.';row.append(cell);root.append(row)}
  else for(const device of [...devices.values()].sort((a,b)=>a.ip.localeCompare(b.ip))){
   const row=document.createElement('tr');
   for(const value of [device.ip,device.network||'No informada',device.mac||'No informada',device.name,[...(device.sources||[])].join(' · ')||'No informada',device.state]){
    const cell=document.createElement('td');cell.textContent=value;row.append(cell);
   }
   const action=document.createElement('td'),validIp=isValidIpv4(device.ip);
   if(!validIp)action.textContent='IP no válida';
   else if(device.arpObserved||device.inventoryImported||device.routerBinding){action.textContent='Pendiente de comprobación';httpCheckCells.set(device.ip,action);httpCheckCellResults.set(device.ip,new Map())}
   else action.textContent='No observada ni importada';
   row.append(action);
   const pingCell=document.createElement('td');
   if(validIp){
    const pingButton=document.createElement('button'),pingResult=document.createElement('div');
    pingButton.type='button';pingButton.className='secondary';pingButton.dataset.devicePing='true';pingButton.textContent='Iniciar ping';
    pingResult.className='muted small';pingResult.textContent='—';
    pingButton.addEventListener('click',()=>startDevicePing(device.ip,pingCell,pingResult));
    pingCell.append(pingButton,pingResult);
   }else pingCell.textContent='IP no válida';
   row.append(pingCell);root.append(row);
  }
  const routerMessage=routerData&&routerData.matched?'Tabla '+routerData.brand+' del gateway '+routerData.gatewayIp+' cargada ('+routerRows.length+' reservas; conexión no confirmada). ':routerData&&routerData.reason==='gateway_mac_pending'?'Esperando identificar la MAC del gateway para elegir una tabla. ':routerData?'No hay tabla guardada para el gateway '+routerData.gatewayIp+' ('+(routerData.gatewayMac||'MAC pendiente')+'). ':'Tabla del gateway no disponible. ';
  feedback.textContent=routerMessage+(devices.size?devices.size+' direcciones cargadas. ':'')+(errors.length?'Algunos datos no estuvieron disponibles: '+errors.join(' · '):'Las reservas no prueban conectividad; solo se listan IP conocidas, sin barrido de red.');
  return arpLoaded||assetsLoaded||routerLoaded;
 }catch(error){feedback.textContent='No se pudo actualizar la tabla: '+error.message;return false}
 finally{button.disabled=false}
}
function createHttpLink(ip,label,port=80){
 const link=document.createElement('a');
 link.href='http://'+ip+':'+port+'/';link.target='_blank';link.rel='noopener noreferrer';
 link.textContent=label;link.setAttribute('aria-label','Abrir servidor HTML en '+ip+' puerto '+port);
 return link;
}
function renderHttpCheckResult(ip,port,data){
 const cell=httpCheckCells.get(ip);
 const results=httpCheckCellResults.get(ip);
 if(results){
  results.set(port,data);
  if(cell){
   cell.replaceChildren();
   for(const checkedPort of [80,8080]){
    const result=results.get(checkedPort);
    if(!result)continue;
    if(cell.childNodes.length)cell.append(document.createTextNode(' · '));
    if(result.html)cell.append(createHttpLink(ip,'Abrir :'+checkedPort,checkedPort));
    else{
     const state=document.createElement('span');
     state.textContent=checkedPort+': '+(result.error?'no comprobado':result.status?'HTTP '+result.status:'sin respuesta');
     cell.append(state);
    }
   }
  }
 }
 renderHttpCheckResultRow(ip,port,data);
}
function renderHttpCheckResultRow(ip,port,data,message=''){
 const root=document.getElementById('httpCheckResultsTable');
 if(!root)return;
 const key=ip+':'+port;
 let row=[...root.querySelectorAll('tr[data-key]')].find(entry=>entry.dataset.key===key);
 if(!row){
  const emptyRow=root.querySelector('[data-empty]');
  if(emptyRow)emptyRow.remove();
  row=document.createElement('tr');row.dataset.key=key;
  for(let i=0;i<6;i++)row.append(document.createElement('td'));
  root.append(row);
 }
 const cells=row.cells;
 cells[0].textContent=ip;cells[1].textContent=String(port);
 cells[2].textContent=message||(data&&data.connected?'Conectado':'Sin conexión TCP');
 cells[3].textContent=data&&Number.isFinite(Number(data.latencyMs))?Math.round(Number(data.latencyMs))+' ms':'—';
 cells[4].textContent=data&&data.status?'HTTP '+data.status:data&&data.connected?'Sin respuesta HTTP':message?'—':'Sin respuesta';
 cells[5].replaceChildren();
 if(data&&data.html)cells[5].append(createHttpLink(ip,'Abrir HTTP',port));
}
function renderPingHttpResult(cell,ip,html){
 if(html){
  const link=createHttpLink(ip,'✓',80);
  link.title='Ping respondido; abrir servidor HTML en '+ip;
  link.style.color='var(--green)';link.style.fontSize='20px';link.style.fontWeight='800';
  cell.replaceChildren(link);
 }else{
  const marker=document.createElement('span');
  marker.textContent='×';marker.title='Ping respondido; sin servidor HTML en puerto 80';
  marker.setAttribute('aria-label','Ping respondido, servidor HTML no disponible');
  marker.style.color='var(--cyan)';marker.style.fontSize='20px';marker.style.fontWeight='800';
  cell.replaceChildren(marker);
 }
}
async function checkLanHttpServers(){
 const button=document.getElementById('checkHttpServersBtn'),feedback=document.getElementById('httpCheckFeedback');
 if(activeHttpCheck){activeHttpCheck.stop=true;feedback.textContent='Deteniendo después de la comprobación actual…';return}
 if(activeDevicePing){feedback.textContent='Finaliza el ping actual antes de comprobar HTTP.';return}
 if(!await loadLanDevices()){feedback.textContent='No se pudieron actualizar ARP e inventario. '+document.getElementById('lanDevicesFeedback').textContent;return}
 if(!lanStaConnected){feedback.textContent='Conecta la STA a la LAN antes de comprobar los servidores HTML.';return}
 const targets=httpCheckEntries.slice(0,32),omittedTargets=httpCheckEntries.length-targets.length;
 const ports=[80,8080],totalChecks=targets.length*ports.length;
 if(!targets.length){feedback.textContent='No hay IP válidas de ARP, inventario o tabla del gateway para comprobar.';return}
 const resultsBody=document.getElementById('httpCheckResultsTable');
 resultsBody.replaceChildren();
 for(const entry of targets)for(const port of ports)renderHttpCheckResultRow(entry.ip,port,null,'Pendiente');
 const run={stop:false};activeHttpCheck=run;button.textContent='Detener comprobación';document.getElementById('refreshDevicesBtn').disabled=true;
 document.querySelectorAll('[data-device-ping]').forEach(pingButton=>pingButton.disabled=true);
 let checked=0,limitMessage='';
 try{
  for(const entry of targets){
   for(const port of ports){
    if(run.stop)break;
    const cell=httpCheckCells.get(entry.ip);
    if(cell)cell.textContent='Comprobando puerto '+port+'…';
    renderHttpCheckResultRow(entry.ip,port,null,'Comprobando…');
    try{
     const data=await(await request('/api/http-server-check?ip='+encodeURIComponent(entry.ip)+'&port='+port)).json();
     renderHttpCheckResult(entry.ip,port,data);
     checked++;
    }catch(error){
     const results=httpCheckCellResults.get(entry.ip);
     if(results)results.set(port,{error:error.message});
     if(cell){
      cell.replaceChildren();
      for(const checkedPort of [80,8080]){
       const result=results&&results.get(checkedPort);
       if(!result)continue;
       if(cell.childNodes.length)cell.append(document.createTextNode(' · '));
       if(result.html)cell.append(createHttpLink(entry.ip,'Abrir :'+checkedPort,checkedPort));
       else{
        const state=document.createElement('span');
        state.textContent=checkedPort+': '+(result.error?'no comprobado':result.status?'HTTP '+result.status:'sin respuesta');
        cell.append(state);
       }
      }
     }
     renderHttpCheckResultRow(entry.ip,port,null,'No comprobado: '+error.message);
     if(error.message.includes('límite')||error.message.includes('Espera')||error.message.includes('Conecta la STA')){limitMessage=error.message;run.stop=true;break}
     checked++;
    }
    feedback.textContent=checked+' de '+totalChecks+' comprobaciones HTTP.'+(omittedTargets?' Se omitieron '+omittedTargets+' IP por el límite de 32.':'');
    if(!run.stop&&checked<totalChecks)await new Promise(resolve=>setTimeout(resolve,1200));
   }
   if(run.stop)break;
  }
  feedback.textContent=limitMessage?'Comprobación detenida: '+limitMessage:run.stop?'Comprobación detenida; '+checked+' comprobaciones realizadas.':checked+' de '+totalChecks+' comprobaciones HTTP completadas. Solo se enlazaron respuestas HTML.'+(omittedTargets?' Se omitieron '+omittedTargets+' IP por el límite de 32.':'');
 }finally{
  if(activeHttpCheck===run)activeHttpCheck=null;
  button.textContent='Corroborar HTML';document.getElementById('refreshDevicesBtn').disabled=false;
  document.querySelectorAll('[data-device-ping]').forEach(pingButton=>pingButton.disabled=false);
 }
}
async function startDevicePing(ip,cell,result){
 if(activeDevicePing||activeHttpCheck)return;
 const run={ip,stop:false,responded:false};activeDevicePing=run;
 const stopButton=document.getElementById('stopDevicePingBtn'),feedback=document.getElementById('devicePingFeedback'),buttons=document.querySelectorAll('[data-device-ping]');
 buttons.forEach(button=>button.disabled=true);stopButton.disabled=false;feedback.textContent='Ping a '+ip+' iniciado; límite de 30 segundos.';
 const deadline=Date.now()+30000;let attempts=0;
 try{
  while(activeDevicePing===run&&!run.stop&&Date.now()<deadline){
   attempts++;
   const data=await(await request('/api/device-ping?ip='+encodeURIComponent(ip))).json();
   result.textContent=data.reachable?'Respondió · '+Number(data.averageMs).toFixed(1)+' ms':'Sin respuesta · intento '+attempts;
   feedback.textContent=data.reachable?'Ping a '+ip+' · '+Number(data.averageMs).toFixed(1)+' ms · '+attempts+' intentos':'Ping a '+ip+' sin respuesta · '+attempts+' intentos';
   if(data.reachable){
    run.responded=true;run.stop=true;
    try{
     const httpData=await(await request('/api/http-server-check?ip='+encodeURIComponent(ip)+'&port=80')).json();
     renderHttpCheckResult(ip,80,httpData);
     renderPingHttpResult(cell,ip,httpData.html===true);
     feedback.textContent=httpData.html?'Ping y servidor HTML confirmados para '+ip+'.':'Ping confirmado para '+ip+'; sin servidor HTML en puerto 80.';
    }catch(error){
     const marker=document.createElement('span');
     marker.textContent='?';marker.title='Ping respondió, pero no se pudo comprobar HTML: '+error.message;
     marker.setAttribute('aria-label','Ping respondió; comprobación HTML pendiente');
     marker.style.color='var(--amber)';marker.style.fontSize='20px';marker.style.fontWeight='800';
     cell.replaceChildren(marker);
     const httpCell=httpCheckCells.get(ip);
     if(httpCell)httpCell.textContent='No comprobado: '+error.message;
     result.textContent='Ping respondió; HTML no comprobado: '+error.message;
     feedback.textContent='No se pudo corroborar HTML para '+ip+': '+error.message;
    }
    break;
   }
   if(Date.now()<deadline&&!run.stop)await new Promise(resolve=>setTimeout(resolve,1000));
  }
  if(run.stop&&!run.responded){result.textContent='Ping finalizado.';feedback.textContent='Ping a '+ip+' finalizado por el usuario.'}
  else if(!run.responded&&Date.now()>=deadline){result.textContent='Límite de 30 s alcanzado.';feedback.textContent='Ping a '+ip+' finalizado al alcanzar el límite de 30 segundos.'}
 }catch(error){result.textContent='Error: '+error.message;feedback.textContent='Ping detenido: '+error.message}
 finally{
  if(activeDevicePing===run)activeDevicePing=null;
  stopButton.disabled=true;document.querySelectorAll('[data-device-ping]').forEach(button=>button.disabled=false);
 }
}
document.getElementById('refreshDevicesBtn').addEventListener('click',loadLanDevices);
document.getElementById('checkHttpServersBtn').addEventListener('click',checkLanHttpServers);
document.getElementById('stopDevicePingBtn').addEventListener('click',()=>{if(activeDevicePing){activeDevicePing.stop=true;document.getElementById('devicePingFeedback').textContent='Deteniendo después del ping actual…'}});
document.querySelectorAll('.nav button').forEach(button=>button.addEventListener('click',()=>{if(button.dataset.tab!=='devices'){if(activeDevicePing)activeDevicePing.stop=true;if(activeHttpCheck)activeHttpCheck.stop=true}}));
document.getElementById('lanDevicesTable').closest('article').after(document.getElementById('httpCheckResultsTable').closest('article'));
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

} // namespace

#if defined(CONFIG_APP_ROLLBACK_ENABLE)
extern "C" bool verifyRollbackLater() { return true; }
#endif

void setup() {
  Serial.begin(115200);
  delay(100);
  errorLogBootId = esp_random();
  errorLogMutex = xSemaphoreCreateMutex();
  if (errorLogMutex == nullptr) {
    Serial.println(F("ERROR: no se pudo crear el bloqueo del log"));
  }
  initializeDeviceIdentity();
  gatewayVendorMutex = xSemaphoreCreateMutex();
  if (gatewayVendorMutex == nullptr) {
    Serial.println(F("ERROR: no se pudo crear el bloqueo del fabricante del gateway"));
    recordError("Inicio", "No se pudo crear el bloqueo del fabricante del gateway");
  }
  Serial.println();
  Serial.println(F("ESP32 Site Survey iniciando"));
  Serial.println(F("Escaneo pasivo; usar solo en redes autorizadas"));
  Serial.printf("Equipo %s · firmware %s · reset %s\n", deviceId.c_str(),
                FIRMWARE_VERSION, deviceResetReason.c_str());

  pinMode(PIN_BOOT, INPUT_PULLUP);
  initializeDisplay();
  startFilesystem();
  startSdCard();
  if (filesystemReady) {
    String initialPoints;
    if (!pointsJson(initialPoints)) {
      Serial.println(F("ERROR: no se pudo cargar el historial de puntos"));
      recordError("LittleFS", "No se pudo cargar el historial de puntos");
    }
    if (!loadCctvAssets()) {
      Serial.println(F("ERROR: no se pudo cargar el inventario CCTV"));
      recordError("LittleFS", "No se pudo cargar el inventario CCTV");
    } else if (sdCardReady && !ensureCctvHistoryFile()) {
      Serial.println(F("ERROR: no se pudo inicializar historial CCTV en microSD"));
      recordError("microSD", "No se pudo inicializar historial CCTV");
    }
  }
  startPreferences();
  recordDeviceBootInfo();
  startAccessPoint();
  configureRoutes();
  initializeStaReconnect();
  startBleProvisioning(false);
  updateDisplay();
  nextOtaCheckAt = millis() + 30000UL;
  verifyPendingOtaImage();
  Serial.println(F("Portal listo en http://192.168.4.1"));
}

void loop() {
  const bool serialConnected = static_cast<bool>(Serial);
  if (serialConnected && !serialWasConnected) {
    dumpErrorLogToSerial();
  }
  serialWasConnected = serialConnected;

  processBleProvisioningEvents();
  handleBootButton();
  if (dnsServerReady) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  maintainStaConnection();
  maintainArpObservations();

  const bool otaCheckDue =
      static_cast<int32_t>(millis() - nextOtaCheckAt) >= 0;
  if (!otaManifestUrl.isEmpty() && !otaUpdateInProgress &&
      ((otaUpdateRequested && otaCheckDue) ||
       (!otaUpdateRequested && (otaCheckRequested || otaCheckDue)))) {
    if (otaUpdateRequested) {
      if (WiFi.status() != WL_CONNECTED) {
        setOtaStatus("Actualización confirmada; esperando conexión a Internet");
        nextOtaCheckAt = millis() + 30000UL;
      } else {
        otaUpdateRequested = false;
        performOtaUpdate();
      }
    } else {
      otaCheckRequested = false;
      const bool checkSucceeded = performOtaCheck();
      nextOtaCheckAt =
          millis() + (checkSucceeded ? OTA_CHECK_INTERVAL_MS : 30000UL);
    }
  }

  if ((bootQrScreen == BootQrScreen::Status) &&
      millis() >= nextScreenRefreshAt) {
    nextScreenRefreshAt = millis() + 1500UL;
    updateDisplay();
  }
  delay(2);
}
