#include "ota.h"
#include "config.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "OTA";

const char* ota_get_version(void)
{
    return FW_VERSION;
}

esp_err_t ota_update_from_url(const char *url)
{
    ESP_LOGI(TAG, "Starte OTA-Update von: %s", url);
    
    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA-Update erfolgreich! Reboot...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA-Update fehlgeschlagen: %s (0x%x)", esp_err_to_name(ret), ret);
        ESP_LOGE(TAG, "URL: %s", url);
        
        if (ret == ESP_ERR_HTTP_CONNECT) {
            ESP_LOGE(TAG, "Kann Server nicht erreichen - prüfe Netzwerk und URL");
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Timeout beim Download - Server zu langsam oder nicht erreichbar");
        } else if (ret == ESP_ERR_HTTP_FETCH_HEADER) {
            ESP_LOGE(TAG, "HTTP-Header-Fehler - Server antwortet nicht korrekt");
        }
    }
    
    return ret;
}

static esp_err_t check_new_version(const char *url, bool *update_available)
{
    // Versions-Check: Lade Version-Info vom Server
    // Format: erste Zeile der Datei sollte Version enthalten
    char version_url[256];
    snprintf(version_url, sizeof(version_url), "%s.version", url);
    
    ESP_LOGI(TAG, "Prüfe Version auf: %s", version_url);
    
    esp_http_client_config_t config = {
        .url = version_url,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP-Client init fehlgeschlagen");
        return ESP_FAIL;
    }
    
    esp_err_t err = esp_http_client_open(client, 0);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Versions-Check fehlgeschlagen: %s (0x%x)", esp_err_to_name(err), err);
        ESP_LOGE(TAG, "URL: %s", version_url);
        
        if (err == ESP_ERR_HTTP_CONNECT) {
            ESP_LOGE(TAG, "Kann OTA-Server nicht erreichen - prüfe FW_UPDATE_URL in config_local.h");
        }
        
        esp_http_client_cleanup(client);
        return err;
    }
    // Lese Versions-Info
    char server_version[32] = {0};
    int content_length = esp_http_client_fetch_headers(client);
    
    if (content_length > 0 && content_length < sizeof(server_version)) {
        esp_http_client_read(client, server_version, content_length);
        server_version[content_length] = '\0';
        
        // Entferne Newlines
        char *newline = strchr(server_version, '\n');
        if (newline) *newline = '\0';
        
        ESP_LOGI(TAG, "Aktuelle Version: %s, Server-Version: %s", 
                 FW_VERSION, server_version);
        
        // Einfacher String-Vergleich (für Semantic Versioning müsste man parsen)
        if (strcmp(server_version, FW_VERSION) != 0) {
            *update_available = true;
            ESP_LOGI(TAG, "Neue Version verfügbar!");
        } else {
            *update_available = false;
        }
    }
    
    esp_http_client_cleanup(client);
    return ESP_OK;
}

void ota_check_and_update_task(void *pvParameters)
{
#if AUTO_UPDATE
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(UPDATE_INTERVAL * 1000));
        
        ESP_LOGI(TAG, "Prüfe auf Firmware-Updates...");
        
        bool update_available = false;
        if (check_new_version(FW_UPDATE_URL, &update_available) == ESP_OK) {
            if (update_available) {
                ESP_LOGI(TAG, "Starte automatisches Update...");
                ota_update_from_url(FW_UPDATE_URL);
            }
        }
    }
#else
    vTaskDelete(NULL);
#endif
}

esp_err_t ota_init(void)
{
#if OTA_ENABLED
    ESP_LOGI(TAG, "OTA initialisiert, Version: %s", FW_VERSION);
    
    // Zeige Partition-Info
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s (offset 0x%08x)", 
             running->label, running->address);
    
#if AUTO_UPDATE
    // Starte Auto-Update-Task
    xTaskCreate(ota_check_and_update_task, "ota_update", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "Auto-Update aktiviert (Check alle %d Sekunden)", UPDATE_INTERVAL);
#endif
    
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
