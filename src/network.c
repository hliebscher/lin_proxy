#include "network.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "NETWORK";
static int udp_sock = -1;
static struct sockaddr_in syslog_addr;
static bool wifi_connected = false;
static bool eth_connected = false;
static bool ap_mode_active = false;
static int wifi_retry_count = 0;
#define WIFI_MAX_RETRY 5  // Max. Versuche für Station-Verbindung

#if USE_ETHERNET
#include "driver/gpio.h"

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_id == ETHERNET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "Ethernet verbunden");
        eth_connected = true;
    } else if (event_id == ETHERNET_EVENT_DISCONNECTED) {
        ESP_LOGI(TAG, "Ethernet getrennt");
        eth_connected = false;
    } else if (event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Ethernet IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t init_ethernet(void)
{
    // PHY Clock auf GPIO16 für KSZ8081RNA
    gpio_set_drive_capability(GPIO_NUM_16, GPIO_DRIVE_CAP_3);
    
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;
    
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_mdc_gpio_num = ETH_PHY_MDC;
    esp32_emac_config.smi_mdio_gpio_num = ETH_PHY_MDIO;
    
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_ksz80xx(&phy_config);
    
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));
    
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &eth_event_handler, NULL));
    
    // KSZ8081RNA PHY-spezifische Register
    uint32_t phy_addr = ETH_PHY_ADDR;
    esp_eth_ioctl(eth_handle, ETH_CMD_S_PHY_ADDR, &phy_addr);
    
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    ESP_LOGI(TAG, "Ethernet gestartet");
    
    return ESP_OK;
}

#else // WiFi-Modus

// Forward-Declaration
static esp_err_t init_wifi_ap(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi Station startet, versuche Verbindung zu '%s'...", WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        
        if (!ap_mode_active && wifi_retry_count < WIFI_MAX_RETRY) {
            wifi_retry_count++;
            ESP_LOGI(TAG, "WiFi Verbindung fehlgeschlagen, Versuch %d/%d", wifi_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else if (!ap_mode_active && wifi_retry_count >= WIFI_MAX_RETRY) {
            ESP_LOGW(TAG, "WiFi Station-Modus fehlgeschlagen nach %d Versuchen", WIFI_MAX_RETRY);
            ESP_LOGI(TAG, "Wechsle zu Access Point-Modus mit fester IP");

            // Stoppe STA, schalte auf AP um und starte neu
            esp_wifi_stop();
            esp_wifi_set_mode(WIFI_MODE_AP);
            init_wifi_ap();
            esp_wifi_start();
            ap_mode_active = true;
        } else if (wifi_connected) {
            // Reconnect-Versuch wenn Station bereits mal verbunden war
            ESP_LOGI(TAG, "WiFi Station getrennt, versuche Reconnect...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi Station verbunden! IP (DHCP): " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        wifi_retry_count = 0;  // Reset bei erfolgreicher Verbindung
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Client " MACSTR " verbunden mit Access Point", MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Access Point gestartet mit fester IP");
    }
}

static esp_err_t init_wifi_ap(void)
{
    // Konfiguriere feste IP für AP-Modus
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_dhcps_stop(ap_netif);  // DHCP-Server stoppen für IP-Änderung

        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);        // AP IP: 192.168.4.1
        IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);        // Gateway
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // Netzmaske

        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);  // DHCP-Server neu starten

        ESP_LOGI(TAG, "AP feste IP konfiguriert: " IPSTR, IP2STR(&ip_info.ip));
    }

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    if (strlen(AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Hinweis: esp_wifi_set_config muss im AP-Modus aufgerufen werden
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "WiFi Access Point konfiguriert: SSID='%s', IP=192.168.4.1", AP_SSID);

    return ESP_OK;
}

static esp_err_t init_wifi(void)
{
    // Erstelle Interfaces für beide Modi (werden nicht gleichzeitig aktiv)
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // Konfiguriere Station-Modus mit DHCP (Priorität 1)
    wifi_config_t sta_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    
    // Starte NUR im Station-Modus (DHCP), AP-Fallback erfolgt bei Fehler
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    // AP wird erst konfiguriert, wenn auf AP-Modus gewechselt wird

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi gestartet im Station-Modus (DHCP)");
    ESP_LOGI(TAG, "Versuche Verbindung zu '%s'...", WIFI_SSID);
    ESP_LOGI(TAG, "Fallback zu AP-Modus '%s' (feste IP) nach %d Fehlversuchen", AP_SSID, WIFI_MAX_RETRY);
    
    return ESP_OK;
}
#endif

esp_err_t network_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
#if USE_ETHERNET
    return init_ethernet();
#else
    return init_wifi();
#endif
}

char* network_get_ip_string(void)
{
    static char ip_str[16] = "unknown";
    
#if USE_ETHERNET
    if (eth_connected) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            }
        }
    }
#else
    if (wifi_connected) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            }
        }
    } else if (ap_mode_active) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (netif) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            }
        }
    }
#endif
    
    return ip_str;
}

void network_log(const char *msg)
{
#if LOG_TO_UDP
    // Prüfe ob Netzwerk verbunden
    if (!wifi_connected && !eth_connected) {
        ESP_LOGD(TAG, "UDP-Log übersprungen: Netzwerk nicht verbunden");
        return;
    }
    
    // Initialisiere UDP-Socket beim ersten Aufruf
    if (udp_sock < 0) {
        udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp_sock < 0) {
            ESP_LOGE(TAG, "UDP-Socket erstellen fehlgeschlagen: errno=%d", errno);
            return;
        }
        
        memset(&syslog_addr, 0, sizeof(syslog_addr));
        syslog_addr.sin_family = AF_INET;
        syslog_addr.sin_port = htons(SYSLOG_PORT);
        
        int ret = inet_pton(AF_INET, SYSLOG_SERVER, &syslog_addr.sin_addr);
        if (ret != 1) {
            ESP_LOGE(TAG, "Syslog-Server IP ungültig: %s", SYSLOG_SERVER);
            close(udp_sock);
            udp_sock = -1;
            return;
        }
        
        ESP_LOGI(TAG, "UDP-Socket initialisiert -> %s:%d", SYSLOG_SERVER, SYSLOG_PORT);
    }
    
    // Sende Syslog-Nachricht
    int sent = sendto(udp_sock, msg, strlen(msg), 0, 
                      (struct sockaddr *)&syslog_addr, sizeof(syslog_addr));
    
    if (sent < 0) {
        ESP_LOGE(TAG, "UDP sendto fehlgeschlagen: errno=%d (%s)", errno, strerror(errno));
        ESP_LOGE(TAG, "Ziel: %s:%d", SYSLOG_SERVER, SYSLOG_PORT);
        // Socket neu initialisieren beim nächsten Aufruf
        close(udp_sock);
        udp_sock = -1;
    } else {
        ESP_LOGD(TAG, "UDP gesendet: %d Bytes an %s:%d", sent, SYSLOG_SERVER, SYSLOG_PORT);
    }
#endif
}
