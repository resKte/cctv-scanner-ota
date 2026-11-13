#include <Arduino.h>
#include <driver/sdmmc_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

namespace {

constexpr gpio_num_t SD_CLK_PIN = GPIO_NUM_14;
constexpr gpio_num_t SD_CMD_PIN = GPIO_NUM_15;
constexpr gpio_num_t SD_D0_PIN = GPIO_NUM_16;
constexpr char FORMAT_CONFIRMATION[] = "FORMATEAR";

bool confirmFormat() {
  const unsigned long waitStarted = millis();
  unsigned long lastPrompt = 0;
  while (millis() - waitStarted < 60000) {
    if (millis() - lastPrompt >= 3000) {
      Serial.println();
      Serial.println("ATENCION: se borraran todos los datos de la microSD.");
      Serial.print("Escribe ");
      Serial.print(FORMAT_CONFIRMATION);
      Serial.println(
          " y pulsa Enter para continuar; cualquier otra entrada cancela.");
      Serial.flush();
      lastPrompt = millis();
    }

    if (Serial.available() == 0) {
      delay(10);
      continue;
    }

    String confirmation = Serial.readStringUntil('\n');
    confirmation.trim();
    return confirmation == FORMAT_CONFIRMATION;
  }

  Serial.println("Cancelado: no se recibio confirmacion en 60 segundos.");
  return false;
}

void formatSdCard() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = SD_CLK_PIN;
  slot.cmd = SD_CMD_PIN;
  slot.d0 = SD_D0_PIN;
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
    Serial.printf("No se pudo inicializar/montar la microSD: %s (0x%x)\n",
                  esp_err_to_name(mountResult),
                  static_cast<unsigned>(mountResult));
    Serial.println(
        "Si aparece INVALID_RESPONSE, el fallo es de comunicacion SDMMC; "
        "no se puede formatear desde este programa.");
    return;
  }

  Serial.printf("Tarjeta detectada: %llu MB\n",
                static_cast<unsigned long long>(card->csd.capacity) /
                    (1024ULL * 1024ULL));

  const esp_err_t formatResult = esp_vfs_fat_sdcard_format("/sdcard", card);
  if (formatResult != ESP_OK) {
    Serial.printf("Fallo el formateo FAT: %s (0x%x)\n",
                  esp_err_to_name(formatResult),
                  static_cast<unsigned>(formatResult));
    esp_vfs_fat_sdcard_unmount("/sdcard", card);
    return;
  }

  Serial.println("Formato FAT completado y tarjeta montada en /sdcard.");
  const esp_err_t unmountResult = esp_vfs_fat_sdcard_unmount("/sdcard", card);
  if (unmountResult != ESP_OK) {
    Serial.printf("Aviso: no se pudo desmontar limpiamente: %s (0x%x)\n",
                  esp_err_to_name(unmountResult),
                  static_cast<unsigned>(unmountResult));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const unsigned long serialWaitStarted = millis();
  while (!Serial && millis() - serialWaitStarted < 10000) {
    delay(10);
  }

  if (!confirmFormat()) {
    Serial.println("Cancelado; la tarjeta no fue formateada.");
    return;
  }
  formatSdCard();
}

void loop() {
  delay(1000);
}
