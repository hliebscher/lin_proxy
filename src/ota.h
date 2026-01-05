#ifndef OTA_H
#define OTA_H

#include "esp_err.h"

// OTA-System initialisieren
esp_err_t ota_init(void);

// Firmware-Update über HTTP durchführen
esp_err_t ota_update_from_url(const char *url);

// Versions-Check und Auto-Update
void ota_check_and_update_task(void *pvParameters);

// Aktuelle Firmware-Version
const char* ota_get_version(void);

#endif // OTA_H
