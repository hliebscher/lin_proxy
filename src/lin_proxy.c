#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "config.h"
#include "network.h"
#include "ota.h"
#include "webserver.h"

#define TAG "LIN_PROXY"

#define LIN_BAUD 9600
#ifndef DEBUG_UART_EVENTS
#define DEBUG_UART_EVENTS 0  // Set to 1 for verbose UART event logs
#endif

// LIN-Protokoll Konstanten
#define LIN_SYNC_BYTE 0x55
#define LIN_MAX_DATA_LEN 8

// LIN1
#define LIN1_UART UART_NUM_1
#define LIN1_RX   GPIO_NUM_14
#define LIN1_TX   GPIO_NUM_15

// LIN2
#define LIN2_UART UART_NUM_2
#define LIN2_RX   GPIO_NUM_13
#define LIN2_TX   GPIO_NUM_12

#define UART_BUF  2048

// Pins erst verarbeiten, wenn sie gesetzt wurden (Pin-Strapping Stabilisierung)
static volatile bool lin1_pins_ready = false;
static volatile bool lin2_pins_ready = false;

// Grenzen für Sync-Suche nach BREAK
#define SYNC_SEARCH_MAX_BYTES 3      // max. Nicht-0x55 Bytes direkt nach BREAK tolerieren
#define SYNC_SEARCH_MAX_US    600    // max. Zeitfenster in µs nach BREAK für SYNC

// Einfacher Antwort-Tracker zwischen LIN1 (Header) und LIN2 (Daten)
typedef struct {
    volatile bool expecting;   // Header gesendet, erwarte Antwort
    volatile bool got;         // Erstes Datenbyte empfangen
    uint8_t id;                // ID des zuletzt gesendeten Headers
    int64_t t_us;              // Zeitstempel des Header-Sendens
} response_tracker_t;

static response_tracker_t g_resp = {0};

typedef enum {
    ST_IDLE = 0,
    ST_GOT_BREAK,
    ST_GOT_SYNC,
    ST_GOT_ID,
    ST_DATA
} lin_state_t;

typedef struct {
    uart_port_t in_uart;
    uart_port_t out_uart;
    gpio_num_t  out_tx_pin;
    QueueHandle_t q;
    lin_state_t st;
    uint8_t last_id;
    const char *name;         // z.B. "LIN1→LIN2"
    uint8_t frame_buf[20];    // Buffer für komplettes Frame
    uint8_t frame_len;        // Länge des aktuellen Frames
    bool is_master;           // true = Master→Slave (Header regenerieren), false = Slave→Master (nur Daten)
    int64_t break_timestamp;  // Timestamp des Break-Events für Timing-Analyse
    int64_t sync_timestamp;   // Timestamp des Sync-Bytes
    int64_t id_timestamp;     // Timestamp des ID-Bytes
    uint8_t sync_search_count; // Anzahl der Nicht-0x55 Bytes nach BREAK
} lin_link_t;

static inline void delay_us(int us) { esp_rom_delay_us(us); }

static void lin_send_break_gpio(gpio_num_t tx_pin, int us_low)
{
    gpio_set_direction(tx_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(tx_pin, 0);
    delay_us(us_low);
    gpio_set_direction(tx_pin, GPIO_MODE_INPUT_OUTPUT);
}

static void lin_send_header(lin_link_t *lnk, uint8_t id)
{
    lin_send_break_gpio(lnk->out_tx_pin, 1500);
    uint8_t hdr[2] = {0x55, id};
    uart_write_bytes(lnk->out_uart, (const char*)hdr, 2);
    
    // Frame-Buffer initialisieren für Logging
    lnk->frame_buf[0] = 0x55;
    lnk->frame_buf[1] = id;
    lnk->frame_len = 2;

    // Antwort-Tracking initialisieren (nur im Masterpfad relevant)
    if (lnk->is_master) {
        g_resp.expecting = true;
        g_resp.got = false;
        g_resp.id = id;
        g_resp.t_us = esp_timer_get_time();
    }
}

static bool is_likely_break_event(uart_event_t *e)
{
    return (e->type == UART_BREAK) || (e->type == UART_FRAME_ERR);
}

// Berechne LIN ID Parität (P0 und P1)
static uint8_t lin_calc_id_parity(uint8_t id_no_parity)
{
    uint8_t p0 = ((id_no_parity >> 0) ^ (id_no_parity >> 1) ^ 
                  (id_no_parity >> 2) ^ (id_no_parity >> 4)) & 1;
    uint8_t p1 = ~((id_no_parity >> 1) ^ (id_no_parity >> 3) ^ 
                   (id_no_parity >> 4) ^ (id_no_parity >> 5)) & 1;
    return (p1 << 7) | (p0 << 6) | (id_no_parity & 0x3F);
}

// Prüfe ob ID-Parität korrekt ist
static bool lin_check_id_parity(uint8_t id_with_parity)
{
    uint8_t expected = lin_calc_id_parity(id_with_parity & 0x3F);
    return expected == id_with_parity;
}

// Berechne Classic Checksum (nur Daten)
static uint8_t lin_calc_checksum_classic(const uint8_t *data, uint8_t len)
{
    uint16_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += data[i];
        if (sum > 0xFF) sum -= 0xFF;  // Carry
    }
    return ~sum;
}

// Berechne Enhanced Checksum (ID + Daten)
static uint8_t lin_calc_checksum_enhanced(uint8_t id, const uint8_t *data, uint8_t len)
{
    uint16_t sum = id;
    for (int i = 0; i < len; i++) {
        sum += data[i];
        if (sum > 0xFF) sum -= 0xFF;
    }
    return ~sum;
}

static void log_lin_frame(lin_link_t *lnk)
{
#if LOG_LIN_FRAMES
    char log_buf[256];
    int offset = snprintf(log_buf, sizeof(log_buf), "[%s] ID=0x%02X Data=", 
                          lnk->name, lnk->last_id);
    
    for (int i = 2; i < lnk->frame_len && offset < sizeof(log_buf) - 4; i++) {
        offset += snprintf(log_buf + offset, sizeof(log_buf) - offset, 
                          "%02X ", lnk->frame_buf[i]);
    }
    
    ESP_LOGI(TAG, "%s", log_buf);
    network_log(log_buf);
#endif
}

#if LIN_SNIFFER_MODE
// Detaillierte Frame-Analyse für Sniffer-Modus
static void sniffer_analyze_frame(lin_link_t *lnk)
{
    char log_buf[512];
    int offset = 0;
    
    // Basis-Informationen
    uint8_t id_raw = lnk->last_id;
    uint8_t id_no_parity = id_raw & 0x3F;
    bool parity_ok = lin_check_id_parity(id_raw);
    
    offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                      "\n========== LIN FRAME ==========\n");
    offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                      "ID: 0x%02X (raw) / 0x%02X (no parity)\n", id_raw, id_no_parity);
    offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                      "ID Parity: %s\n", parity_ok ? "OK" : "FEHLER!");
    
#if SNIFFER_DETAIL_LOGS
    // Timing-Analyse
    if (lnk->break_timestamp > 0 && lnk->sync_timestamp > 0) {
        int64_t break_to_sync = lnk->sync_timestamp - lnk->break_timestamp;
        offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                          "Break→Sync: %lld µs\n", break_to_sync);
    }
    if (lnk->sync_timestamp > 0 && lnk->id_timestamp > 0) {
        int64_t sync_to_id = lnk->id_timestamp - lnk->sync_timestamp;
        offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                          "Sync→ID: %lld µs\n", sync_to_id);
    }
#endif
    
    // Daten-Bytes
    int data_len = lnk->frame_len - 2;  // Ohne Sync + ID
    offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                      "Data Length: %d bytes\n", data_len);
    
    if (data_len > 0) {
        offset += snprintf(log_buf + offset, sizeof(log_buf) - offset, "Data: ");
        for (int i = 0; i < data_len && offset < sizeof(log_buf) - 20; i++) {
            offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                              "%02X ", lnk->frame_buf[2 + i]);
        }
        offset += snprintf(log_buf + offset, sizeof(log_buf) - offset, "\n");
        
        // Checksumme prüfen (letztes Byte ist Checksumme)
        if (data_len >= 2) {
            uint8_t checksum_received = lnk->frame_buf[lnk->frame_len - 1];
            uint8_t checksum_classic = lin_calc_checksum_classic(&lnk->frame_buf[2], data_len - 1);
            uint8_t checksum_enhanced = lin_calc_checksum_enhanced(id_raw, &lnk->frame_buf[2], data_len - 1);
            
            offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                              "Checksum: 0x%02X (received)\n", checksum_received);
            offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                              "  Classic:  0x%02X %s\n", checksum_classic,
                              checksum_classic == checksum_received ? "✓" : "✗");
            offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                              "  Enhanced: 0x%02X %s\n", checksum_enhanced,
                              checksum_enhanced == checksum_received ? "✓" : "✗");
        }
    }
    
    offset += snprintf(log_buf + offset, sizeof(log_buf) - offset,
                      "==============================\n");
    
    ESP_LOGI(TAG, "%s", log_buf);
    network_log(log_buf);
}

// Sniffer-Task für LIN1 (nur Listen, kein Weiterleiten)
static void lin_sniffer_task(void *arg)
{
    lin_link_t *lnk = (lin_link_t*)arg;
    uart_event_t e;
    uint8_t b;
    
    ESP_LOGI(TAG, "[SNIFFER] Task gestartet auf %s (nur Analyse, kein Proxy!)", lnk->name);
    ESP_LOGI(TAG, "[SNIFFER] Warte auf LIN-Traffic...");

    while (1) {
        if (xQueueReceive(lnk->q, &e, portMAX_DELAY) != pdTRUE) continue;

        // Handle overflow
        if (e.type == UART_FIFO_OVF || e.type == UART_BUFFER_FULL) {
            ESP_LOGW(TAG, "[SNIFFER] UART overflow -> flush");
            uart_flush_input(lnk->in_uart);
            xQueueReset(lnk->q);
            lnk->st = ST_IDLE;
            lnk->frame_len = 0;
            continue;
        }

        // Break-Detection
        if (is_likely_break_event(&e)) {
            // Frame abschließen falls noch Daten da
            if (lnk->st == ST_DATA && lnk->frame_len > 2) {
                sniffer_analyze_frame(lnk);
            }
            
            lnk->break_timestamp = esp_timer_get_time();
            ESP_LOGI(TAG, "[SNIFFER] >>> BREAK erkannt <<<");
            lnk->st = ST_GOT_BREAK;
            lnk->frame_len = 0;
            continue;
        }

        if (e.type == UART_DATA) {
            int len = e.size;
            while (len-- > 0) {
                if (uart_read_bytes(lnk->in_uart, &b, 1, 0) != 1) break;

                switch (lnk->st) {
                    case ST_IDLE:
                        // Im IDLE ignorieren (kein Frame aktiv)
                        ESP_LOGD(TAG, "[SNIFFER] IDLE: Byte 0x%02X (ignoriert)", b);
                        break;

                    case ST_GOT_BREAK:
                        if (b == LIN_SYNC_BYTE) {
                            lnk->sync_timestamp = esp_timer_get_time();
                            lnk->frame_buf[0] = b;
                            lnk->frame_len = 1;
                            lnk->st = ST_GOT_SYNC;
                            ESP_LOGI(TAG, "[SNIFFER] SYNC: 0x%02X", b);
                        } else {
                            ESP_LOGW(TAG, "[SNIFFER] Nach BREAK kein SYNC: 0x%02X -> IDLE", b);
                            lnk->st = ST_IDLE;
                        }
                        break;

                    case ST_GOT_SYNC:
                        lnk->id_timestamp = esp_timer_get_time();
                        lnk->last_id = b;
                        lnk->frame_buf[lnk->frame_len++] = b;
                        lnk->st = ST_GOT_ID;
                        ESP_LOGI(TAG, "[SNIFFER] ID: 0x%02X", b);
                        break;

                    case ST_GOT_ID:
                    case ST_DATA:
                        if (lnk->frame_len < sizeof(lnk->frame_buf)) {
                            lnk->frame_buf[lnk->frame_len++] = b;
                        }
                        lnk->st = ST_DATA;
                        ESP_LOGD(TAG, "[SNIFFER] Data[%d]: 0x%02X", lnk->frame_len - 2, b);
                        
                        // Bei typischer Frame-Länge (2-9 Bytes Daten+Checksum) Frame abschließen
                        if (lnk->frame_len >= 3 && lnk->frame_len <= 10) {
                            // Warte kurz ob noch mehr Daten kommen (Inter-Byte-Timeout)
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }
                        break;
                }
            }
        }
    }
}
#endif

static void uart_init_lin(uart_port_t uart, int tx, int rx, QueueHandle_t *out_q)
{
    uart_config_t cfg = {
        .baud_rate = LIN_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk= UART_SCLK_APB
    };

    // Erst Konfiguration setzen, dann Treiber installieren (stabiler laut ESP-IDF Praxis)
    uart_param_config(uart, &cfg);
    uart_driver_install(uart, UART_BUF, UART_BUF, 20, out_q, 0);
    uart_set_rx_timeout(uart, 2);   // Kurzes Timeout, um Frames schneller abzuschließen
}

static void uart_apply_pins_delayed(void *arg)
{
    // Warte bewusst bis System vollständig gebootet hat (Strapping stabil)
    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Starte UART-Pin-Konfiguration...");
    
    // LIN1 Pins setzen
    ESP_LOGI(TAG, "Setze LIN1 Pins: TX=%d RX=%d", (int)LIN1_TX, (int)LIN1_RX);
    esp_err_t ret1 = uart_set_pin(LIN1_UART, LIN1_TX, LIN1_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "LIN1 uart_set_pin: %s", ret1 == ESP_OK ? "OK" : esp_err_to_name(ret1));
    // RX mit Pullup stabilisieren, sonst können vor der Pin-Zuweisung Zufalls-Events auftreten
    gpio_set_pull_mode(LIN1_RX, GPIO_PULLUP_ONLY);
    uart_flush_input(LIN1_UART);
    lin1_pins_ready = true;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // LIN2 Pins setzen
    ESP_LOGI(TAG, "Setze LIN2 Pins: TX=%d RX=%d", (int)LIN2_TX, (int)LIN2_RX);
    esp_err_t ret2 = uart_set_pin(LIN2_UART, LIN2_TX, LIN2_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "LIN2 uart_set_pin: %s", ret2 == ESP_OK ? "OK" : esp_err_to_name(ret2));
    gpio_set_pull_mode(LIN2_RX, GPIO_PULLUP_ONLY);
    uart_flush_input(LIN2_UART);
    lin2_pins_ready = true;
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "UART-Pins konfiguriert! Warte auf LIN-Events...");
    
    // Periodisches Lebenszeichen
    int alive_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "System läuft... (%d)", ++alive_count);
    }
}

static void lin_proxy_task(void *arg)
{
    lin_link_t *lnk = (lin_link_t*)arg;
    uart_event_t e;
    uint8_t b;
    
    ESP_LOGI(TAG, "[%s] Proxy-Task gestartet (%s)", lnk->name, lnk->is_master ? "Master→Slave" : "Slave→Master");

    while (1) {
        if (xQueueReceive(lnk->q, &e, portMAX_DELAY) != pdTRUE) continue;

        // Warte bis die entsprechenden Pins gesetzt wurden, sonst ignorieren wir Früh-Events
        if ((lnk->in_uart == LIN1_UART && !lin1_pins_ready) ||
            (lnk->in_uart == LIN2_UART && !lin2_pins_ready)) {
            uart_flush_input(lnk->in_uart);
            xQueueReset(lnk->q);
            lnk->st = ST_IDLE;
            lnk->frame_len = 0;
            continue;
        }

#if DEBUG_UART_EVENTS
        ESP_LOGI(TAG, "[%s] UART event type=%d size=%d", lnk->name, e.type, e.size);
#endif

        // Handle overflow/queue full to avoid stuck RX
        if (e.type == UART_FIFO_OVF || e.type == UART_BUFFER_FULL) {
            ESP_LOGW(TAG, "[%s] UART overflow/buffer full -> flush", lnk->name);
            uart_flush_input(lnk->in_uart);
            xQueueReset(lnk->q);
            lnk->st = ST_IDLE;
            lnk->frame_len = 0;
            continue;
        }

        // Slave→Master: Nur Daten blind durchreichen, keine Break-Detection
        if (!lnk->is_master) {
            if (e.type == UART_DATA) {
                uint8_t buf[128];
                int len = uart_read_bytes(lnk->in_uart, buf, e.size > sizeof(buf) ? sizeof(buf) : e.size, 0);
                if (len > 0) {
                    // Antwort-Latenz beim ersten Byte messen
                    if (g_resp.expecting && !g_resp.got) {
                        g_resp.got = true;
                        int64_t now = esp_timer_get_time();
                        int64_t dt = now - g_resp.t_us;
                        ESP_LOGI(TAG, "[%s] Antwort auf ID 0x%02X nach %lld µs (%d Bytes)", lnk->name, g_resp.id, dt, len);
                        char m[128];
                        snprintf(m, sizeof(m), "Response for ID 0x%02X in %lldus", g_resp.id, dt);
                        network_log(m);
                    }
                    uart_write_bytes(lnk->out_uart, (const char*)buf, len);
                    ESP_LOGD(TAG, "[%s] Slave-Response: %d Bytes durchgereicht", lnk->name, len);
                }
            }
            continue;
        }

        // Ab hier: Master→Slave mit voller LIN-Protokoll-Verarbeitung
        
        if (is_likely_break_event(&e)) {
            // Bei neuem Break: vorheriges Frame loggen falls vorhanden
            if (lnk->st == ST_DATA && lnk->frame_len > 2) {
                log_lin_frame(lnk);
            }
            
            // Wenn wir auf eine Antwort gewartet haben, aber bis zum nächsten BREAK nichts kam
            if (lnk->is_master && g_resp.expecting && !g_resp.got) {
                ESP_LOGW(TAG, "[%s] KEINE Antwort auf ID 0x%02X innerhalb eines Zyklus", lnk->name, g_resp.id);
                char buf[96];
                snprintf(buf, sizeof(buf), "No response for ID 0x%02X", g_resp.id);
                network_log(buf);
                g_resp.expecting = false;
            }

            ESP_LOGI(TAG, "[%s] BREAK erkannt! (event type=%d, prev_state=%d)", lnk->name, e.type, lnk->st);
            // Flush, um evtl. 0x00/Rauschen aus dem BREAK zu entfernen
            uart_flush_input(lnk->in_uart);
            lnk->st = ST_GOT_BREAK;
            lnk->frame_len = 0;
            lnk->break_timestamp = esp_timer_get_time();
            lnk->sync_search_count = 0;
            continue;
        }
        
        // UART Pattern Detection oder Timeout während Frame-Empfang
        if (e.type == UART_PATTERN_DET || e.type == UART_EVENT_MAX) {
            if (lnk->st == ST_DATA && lnk->frame_len > 2) {
                log_lin_frame(lnk);
                lnk->st = ST_IDLE;
            }
            continue;
        }

        if (e.type == UART_DATA) {
            int len = e.size;
            while (len-- > 0) {
                if (uart_read_bytes(lnk->in_uart, &b, 1, 0) != 1) break;

                switch (lnk->st) {
                    case ST_IDLE:
                        // Im Master-Modus keine unbekannten Bytes durchreichen
                        if (!lnk->is_master) {
                            uart_write_bytes(lnk->out_uart, (char*)&b, 1);
                        }
                        break;

                    case ST_GOT_BREAK:
                        if (b == 0x00) {
                            // 0x00 nach BREAK kommt häufig vom langen Low (Framing Error)
                            ESP_LOGD(TAG, "[%s] Ignoriere 0x00 direkt nach BREAK", lnk->name);
                            break;
                        }
                        // Prüfe Zeitfenster seit BREAK
                        int64_t now_us = esp_timer_get_time();
                        int64_t since_break = now_us - lnk->break_timestamp;

                        if (b == LIN_SYNC_BYTE) {
                            ESP_LOGI(TAG, "[%s] SYNC (0x55) empfangen", lnk->name);
                            lnk->st = ST_GOT_SYNC;
                        } else {
                            lnk->sync_search_count++;
                            if (lnk->sync_search_count <= SYNC_SEARCH_MAX_BYTES && since_break <= SYNC_SEARCH_MAX_US) {
                                // Ignoriere sporadische Bytes im Sync-Fenster
                                ESP_LOGD(TAG, "[%s] Ignoriere 0x%02X im Sync-Fenster (%d/%d, %lldus)",
                                         lnk->name, b, lnk->sync_search_count, SYNC_SEARCH_MAX_BYTES, since_break);
                                break;
                            }
                            ESP_LOGW(TAG, "[%s] Nach BREAK kein SYNC, sondern 0x%02X -> IDLE (count=%d, %lldus)",
                                     lnk->name, b, lnk->sync_search_count, since_break);
                            lnk->st = ST_IDLE;
                        }
                        break;

                    case ST_GOT_SYNC:
                        lnk->last_id = b;
                        // Prüfe ID-Parität; verwerfe Frame bei Fehler
                        if (!lin_check_id_parity(b)) {
                            ESP_LOGW(TAG, "[%s] ID-Parität ungültig: 0x%02X -> Frame verworfen", lnk->name, b);
                            lnk->st = ST_IDLE;
                            break;
                        }
                        ESP_LOGI(TAG, "[%s] ID=0x%02X empfangen, sende Header", lnk->name, b);
                        lin_send_header(lnk, b);
                        lnk->st = ST_GOT_ID;
                        break;

                    case ST_GOT_ID:
                        uart_write_bytes(lnk->out_uart, (char*)&b, 1);
                        if (lnk->frame_len < sizeof(lnk->frame_buf)) {
                            lnk->frame_buf[lnk->frame_len++] = b;
                        }
                        lnk->st = ST_DATA;
                        break;

                    case ST_DATA:
                        uart_write_bytes(lnk->out_uart, (char*)&b, 1);
                        if (lnk->frame_len < sizeof(lnk->frame_buf)) {
                            lnk->frame_buf[lnk->frame_len++] = b;
                        }
                        break;
                }
            }
        }
    }
}

void app_main(void)
{
    // Netzwerk initialisieren (WiFi oder Ethernet)
    ESP_LOGI(TAG, "=== LIN Proxy v%s ===", ota_get_version());
    ESP_LOGI(TAG, "Starte Netzwerk...");
    network_init();
    
    vTaskDelay(pdMS_TO_TICKS(3000)); // Warte auf Netzwerk-Verbindung
    
    // OTA-System initialisieren
    ota_init();
    
    // Web-Server starten
    webserver_init();
    
    QueueHandle_t q1 = NULL;
    QueueHandle_t q2 = NULL;

    uart_init_lin(LIN1_UART, LIN1_TX, LIN1_RX, &q1);
    
#if LIN_SNIFFER_MODE
    // SNIFFER-MODUS: Nur LIN1 analysieren, kein LIN2, kein Proxy
    ESP_LOGW(TAG, "*** SNIFFER-MODUS AKTIVIERT ***");
    ESP_LOGW(TAG, "*** NUR LIN1 WIRD ANALYSIERT (KEIN PROXY!) ***");
    
    static lin_link_t sniffer = {
        .in_uart = LIN1_UART,
        .out_uart = UART_NUM_MAX,  // Kein Output
        .out_tx_pin = GPIO_NUM_NC,
        .st = ST_IDLE,
        .name = "LIN1-SNIFFER",
        .frame_len = 0,
        .is_master = true,
        .break_timestamp = 0,
        .sync_timestamp = 0,
        .id_timestamp = 0
    };
    sniffer.q = q1;
    
    xTaskCreate(lin_sniffer_task, "lin1_sniffer", 6144, &sniffer, 12, NULL);
    ESP_LOGI(TAG, "LIN1 Sniffer gestartet (9600 baud)");
#else
    // PROXY-MODUS: Normale Bidirektionale Weiterleitung
    uart_init_lin(LIN2_UART, LIN2_TX, LIN2_RX, &q2);

    static lin_link_t l12 = {
        .in_uart = LIN1_UART,
        .out_uart = LIN2_UART,
        .out_tx_pin = LIN2_TX,
        .st = ST_IDLE,
        .name = "LIN1→LIN2",
        .frame_len = 0,
        .is_master = true,  // LIN1 ist Master, Header regenerieren
        .break_timestamp = 0,
        .sync_timestamp = 0,
        .id_timestamp = 0
    };
    l12.q = q1;

    static lin_link_t l21 = {
        .in_uart = LIN2_UART,
        .out_uart = LIN1_UART,
        .out_tx_pin = LIN1_TX,
        .st = ST_IDLE,
        .name = "LIN2→LIN1",
        .frame_len = 0,
        .is_master = false,  // LIN2 ist Slave, nur Daten durchreichen
        .break_timestamp = 0,
        .sync_timestamp = 0,
        .id_timestamp = 0
    };
    l21.q = q2;

    xTaskCreate(lin_proxy_task, "lin1_to_lin2", 4096, &l12, 12, NULL);
    xTaskCreate(lin_proxy_task, "lin2_to_lin1", 4096, &l21, 12, NULL);

    ESP_LOGI(TAG, "LIN proxy gestartet (9600 baud)");
#endif

    // UART-Pins erst nach Boot stabilisieren/configurieren
    xTaskCreate(uart_apply_pins_delayed, "uart_pins_late", 2048, NULL, 10, NULL);
    
    // Hole aktuelle IP-Adresse
    const char *ip = network_get_ip_string();
    
#if USE_ETHERNET
    ESP_LOGI(TAG, "Netzwerk-Modus: Ethernet");
    ESP_LOGI(TAG, "IP-Adresse: %s", ip);
#else
    ESP_LOGI(TAG, "Netzwerk-Modus: WiFi (STA+AP)");
    ESP_LOGI(TAG, "WiFi SSID: %s / AP SSID: %s", WIFI_SSID, AP_SSID);
    ESP_LOGI(TAG, "IP-Adresse: %s", ip);
#endif
    
#if LOG_TO_UDP
    ESP_LOGI(TAG, "UDP-Logging aktiviert -> %s:%d", SYSLOG_SERVER, SYSLOG_PORT);
    
    // Sende Test-Nachricht an Syslog-Server
    char startup_msg[256];
    snprintf(startup_msg, sizeof(startup_msg), 
             "[%s] === LIN Proxy v%s gestartet === IP: %s",
             TAG, ota_get_version(), ip);
    network_log(startup_msg);
    ESP_LOGI(TAG, "Syslog Test-Nachricht gesendet");
#endif

#if WEB_SERVER_ENABLED
    ESP_LOGI(TAG, "Web-Interface: http://%s:%d", ip, WEB_SERVER_PORT);
#endif

#if AUTO_UPDATE
    ESP_LOGI(TAG, "Auto-Update aktiviert (Check alle %d Sekunden)", UPDATE_INTERVAL);
    ESP_LOGI(TAG, "OTA-Server: %s", FW_UPDATE_URL);
#endif
}