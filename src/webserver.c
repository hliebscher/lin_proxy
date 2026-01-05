#include "webserver.h"
#include "config.h"
#include "ota.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include <string.h>

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "WEBSERVER";
static httpd_handle_t server = NULL;

// HTML-Seite für Web-Interface
static const char* html_page = 
"<!DOCTYPE html>"
"<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>LIN Proxy</title>"
"<style>"
"body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0}"
"h1{color:#333}.box{background:white;padding:20px;margin:10px 0;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}"
".info{display:flex;justify-content:space-between;margin:10px 0}"
"button{background:#007bff;color:white;border:none;padding:10px 20px;cursor:pointer;border-radius:3px;font-size:16px}"
"button:hover{background:#0056b3}"
".upload{margin:20px 0}"
"input[type=file]{margin:10px 0}"
".status{padding:10px;margin:10px 0;border-radius:3px}"
".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}"
".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}"
"</style></head><body>"
"<h1>🚗 LIN Proxy Control</h1>"
"<div class='box'><h2>System Info</h2>"
"<div class='info'><span>Firmware Version:</span><span id='version'>%s</span></div>"
"<div class='info'><span>WiFi SSID:</span><span>" WIFI_SSID "</span></div>"
"<div class='info'><span>AP SSID:</span><span>" AP_SSID "</span></div>"
"</div>"
"<div class='box'><h2>Firmware Update</h2>"
"<div class='upload'>"
"<input type='file' id='firmwareFile' accept='.bin'>"
"<button onclick='uploadFirmware()'>Upload Firmware</button>"
"</div>"
"<button onclick='checkUpdate()'>Check for Updates</button>"
"<div id='status'></div>"
"</div>"
"<div class='box'><h2>Actions</h2>"
"<button onclick='reboot()'>Reboot ESP32</button>"
"</div>"
"<script>"
"document.getElementById('version').textContent='%s';"
"function showStatus(msg,isError){"
"const s=document.getElementById('status');"
"s.innerHTML='<div class=\"status '+(isError?'error':'success')+'\">'+msg+'</div>';}"
"async function uploadFirmware(){"
"const file=document.getElementById('firmwareFile').files[0];"
"if(!file){showStatus('Bitte Datei auswählen',true);return;}"
"showStatus('Uploading...',false);"
"const formData=new FormData();formData.append('file',file);"
"try{"
"const r=await fetch('/upload',{method:'POST',body:formData});"
"if(r.ok){showStatus('Update erfolgreich! Reboot...',false);setTimeout(()=>location.reload(),5000);}"
"else{showStatus('Upload fehlgeschlagen',true);}"
"}catch(e){showStatus('Fehler: '+e,true);}}"
"async function checkUpdate(){"
"showStatus('Prüfe Updates...',false);"
"try{"
"const r=await fetch('/check-update');"
"const d=await r.json();"
"if(d.available){showStatus('Neue Version verfügbar: '+d.version,false);}"
"else{showStatus('Aktuelle Version ist aktuell',false);}"
"}catch(e){showStatus('Check fehlgeschlagen',true);}}"
"async function reboot(){"
"if(confirm('ESP32 neu starten?')){"
"await fetch('/reboot');showStatus('Rebooting...',false);}}"
"</script></body></html>";

// Handler: Hauptseite
static esp_err_t root_handler(httpd_req_t *req)
{
    const char *version = ota_get_version();
    char response[4096];
    snprintf(response, sizeof(response), html_page, version, version);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

// Handler: Firmware-Upload
static esp_err_t upload_handler(httpd_req_t *req)
{
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Starting OTA to partition: %s", update_partition->label);
    
    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }
    
    char buf[1024];
    int remaining = req->content_len;
    int received = 0;
    
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }
        
        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        
        received += recv_len;
        remaining -= recv_len;
        ESP_LOGI(TAG, "OTA Progress: %d%%", (received * 100) / req->content_len);
    }
    
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }
    
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }
    
    httpd_resp_sendstr(req, "OK");
    
    ESP_LOGI(TAG, "OTA Success! Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

// Handler: Update-Check
static esp_err_t check_update_handler(httpd_req_t *req)
{
    // Simulierter Update-Check (in Produktion mit FW_UPDATE_URL prüfen)
    char json_response[256];
    snprintf(json_response, sizeof(json_response), 
             "{\"available\":false,\"current\":\"%s\",\"latest\":\"%s\"}", 
             ota_get_version(), ota_get_version());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response);
    return ESP_OK;
}

// Handler: Reboot
static esp_err_t reboot_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t webserver_init(void)
{
#if WEB_SERVER_ENABLED
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 8;
    
    ESP_LOGI(TAG, "Starte HTTP-Server auf Port %d", WEB_SERVER_PORT);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
        };
        httpd_register_uri_handler(server, &root);
        
        httpd_uri_t upload = {
            .uri = "/upload",
            .method = HTTP_POST,
            .handler = upload_handler,
        };
        httpd_register_uri_handler(server, &upload);
        
        httpd_uri_t check = {
            .uri = "/check-update",
            .method = HTTP_GET,
            .handler = check_update_handler,
        };
        httpd_register_uri_handler(server, &check);
        
        httpd_uri_t reboot = {
            .uri = "/reboot",
            .method = HTTP_GET,
            .handler = reboot_handler,
        };
        httpd_register_uri_handler(server, &reboot);
        
        ESP_LOGI(TAG, "Web-Interface verfügbar unter http://<IP>:%d", WEB_SERVER_PORT);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "HTTP-Server-Start fehlgeschlagen");
    return ESP_FAIL;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void webserver_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}
