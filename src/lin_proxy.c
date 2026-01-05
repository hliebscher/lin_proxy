#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define TAG "LIN_PROXY"

#define LIN_BAUD 9600

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
}

static bool is_likely_break_event(uart_event_t *e)
{
    return (e->type == UART_BREAK) || (e->type == UART_FRAME_ERR);
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

        if (is_likely_break_event(&e)) {
            lnk->st = ST_GOT_BREAK;
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
                        lnk->st = ST_DATA;
                        break;

                    case ST_DATA:
                        uart_write_bytes(lnk->out_uart, (char*)&b, 1);
                        break;
                }
            }
        }
    }
}

void app_main(void)
{
    QueueHandle_t q1 = NULL;
    QueueHandle_t q2 = NULL;

    uart_init_lin(LIN1_UART, LIN1_TX, LIN1_RX, &q1);
    uart_init_lin(LIN2_UART, LIN2_TX, LIN2_RX, &q2);

    static lin_link_t l12 = {
        .in_uart = LIN1_UART,
        .out_uart = LIN2_UART,
        .out_tx_pin = LIN2_TX,
        .st = ST_IDLE
    };
    l12.q = q1;

    static lin_link_t l21 = {
        .in_uart = LIN2_UART,
        .out_uart = LIN1_UART,
        .out_tx_pin = LIN1_TX,
        .st = ST_IDLE
    };
    l21.q = q2;

    xTaskCreate(lin_proxy_task, "lin1_to_lin2", 4096, &l12, 12, NULL);
    xTaskCreate(lin_proxy_task, "lin2_to_lin1", 4096, &l21, 12, NULL);

    ESP_LOGI(TAG, "LIN proxy gestartet (9600 baud)");
}