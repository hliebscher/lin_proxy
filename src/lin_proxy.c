#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "config.h"
#include "network.h"
#include "ota.h"
#include "webserver.h"

#define TAG "LIN_PROXY"

#define LIN_BAUD 9600
#ifndef DEBUG_UART_EVENTS
#define DEBUG_UART_EVENTS 0  // Set to 1 for verbose UART event logs
#endif

// LIN1
#define LIN1_UART UART_NUM_1
#define LIN1_RX   GPIO_NUM_14
#define LIN1_TX   GPIO_NUM_15

// LIN2
#define LIN2_UART UART_NUM_2
#define LIN2_RX   GPIO_NUM_13
#define LIN2_TX   GPIO_NUM_12

#define UART_BUF  2048

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
}

static bool is_likely_break_event(uart_event_t *e)
{
    return (e->type == UART_BREAK) || (e->type == UART_FRAME_ERR);
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

    uart_driver_install(uart, UART_BUF, UART_BUF, 20, out_q, 0);
    uart_param_config(uart, &cfg);
    uart_set_pin(uart, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_rx_timeout(uart, 2);
}

static void lin_proxy_task(void *arg)
{
    lin_link_t *lnk = (lin_link_t*)arg;
    uart_event_t e;
    uint8_t b;

    while (1) {
        if (xQueueReceive(lnk->q, &e, portMAX_DELAY) != pdTRUE) continue;

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

        if (is_likely_break_event(&e)) {
            // Bei neuem Break: vorheriges Frame loggen falls vorhanden
            if (lnk->st == ST_DATA && lnk->frame_len > 2) {
                log_lin_frame(lnk);
            }
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
                        uart_write_bytes(lnk->out_uart, (char*)&b, 1);
                        break;

                    case ST_GOT_BREAK:
                        if (b == 0x55) lnk->st = ST_GOT_SYNC;
                        else {
                            lnk->st = ST_IDLE;
                            uart_write_bytes(lnk->out_uart, (char*)&b, 1);
                        }
                        break;

                    case ST_GOT_SYNC:
                        lnk->last_id = b;
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
    uart_init_lin(LIN2_UART, LIN2_TX, LIN2_RX, &q2);

    static lin_link_t l12 = {
        .in_uart = LIN1_UART,
        .out_uart = LIN2_UART,
        .out_tx_pin = LIN2_TX,
        .st = ST_IDLE,
        .name = "LIN1→LIN2",
        .frame_len = 0
    };
    l12.q = q1;

    static lin_link_t l21 = {
        .in_uart = LIN2_UART,
        .out_uart = LIN1_UART,
        .out_tx_pin = LIN1_TX,
        .st = ST_IDLE,
        .name = "LIN2→LIN1",
        .frame_len = 0
    };
    l21.q = q2;

    xTaskCreate(lin_proxy_task, "lin1_to_lin2", 4096, &l12, 12, NULL);
    xTaskCreate(lin_proxy_task, "lin2_to_lin1", 4096, &l21, 12, NULL);

    ESP_LOGI(TAG, "LIN proxy gestartet (9600 baud)");
    
#if USE_ETHERNET
    ESP_LOGI(TAG, "Netzwerk-Modus: Ethernet");
#else
    ESP_LOGI(TAG, "Netzwerk-Modus: WiFi (STA+AP)");
    ESP_LOGI(TAG, "WiFi SSID: %s / AP SSID: %s", WIFI_SSID, AP_SSID);
#endif
    
#if LOG_TO_UDP
    ESP_LOGI(TAG, "UDP-Logging aktiviert -> %s:%d", SYSLOG_SERVER, SYSLOG_PORT);
#endif

#if WEB_SERVER_ENABLED
    ESP_LOGI(TAG, "Web-Interface: http://<IP>:%d", WEB_SERVER_PORT);
#endif

#if AUTO_UPDATE
    ESP_LOGI(TAG, "Auto-Update aktiviert (Check alle %d Sekunden)", UPDATE_INTERVAL);
#endif
}