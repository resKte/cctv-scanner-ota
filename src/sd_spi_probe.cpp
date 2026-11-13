#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_log.h>

namespace {

constexpr int SD_CLK_PIN = 14;
constexpr int SD_CMD_PIN = 15;
constexpr int SD_D0_PIN = 16;
constexpr int SD_D3_PIN = 21;
constexpr uint32_t SD_SPI_FREQUENCY = 400000;

void probeCard() {
  Serial.println("Prueba microSD por SPI; no se escribiran ni formatearan datos.");
  Serial.printf("CLK=%d, MOSI/CMD=%d, MISO/D0=%d, CS/D3=%d\n", SD_CLK_PIN,
                SD_CMD_PIN, SD_D0_PIN, SD_D3_PIN);

  if (!SPI.begin(SD_CLK_PIN, SD_D0_PIN, SD_CMD_PIN, SD_D3_PIN)) {
    Serial.println("No se pudo iniciar el bus SPI.");
    return;
  }

  if (!SD.begin(SD_D3_PIN, SPI, SD_SPI_FREQUENCY, "/sdcard", 2, false)) {
    Serial.println(
        "SPI no pudo inicializar y montar la tarjeta; no se realizaron escrituras.");
    SD.end();
    SPI.end();
    return;
  }

  Serial.printf("Tarjeta detectada por SPI: tipo=%u, capacidad=%llu MB\n",
                static_cast<unsigned>(SD.cardType()),
                static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));
  Serial.printf("Sistema de archivos montado: total=%llu MB, usado=%llu MB\n",
                static_cast<unsigned long long>(SD.totalBytes() / (1024ULL * 1024ULL)),
                static_cast<unsigned long long>(SD.usedBytes() / (1024ULL * 1024ULL)));

  SD.end();
  SPI.end();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const unsigned long waitStarted = millis();
  while (!Serial && millis() - waitStarted < 10000) {
    delay(10);
  }

  esp_log_level_set("*", ESP_LOG_DEBUG);
  delay(500);
  probeCard();
}

void loop() {
  delay(10000);
  probeCard();
}
