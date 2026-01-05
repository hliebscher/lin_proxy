#ifndef CONFIG_H
#define CONFIG_H

// Netzwerk-Modus: 0=WiFi, 1=Ethernet
#define USE_ETHERNET 0

// WiFi Station Konfiguration
#define WIFI_SSID       "MeinWLAN"
#define WIFI_PASSWORD   "MeinPasswort"

// WiFi Access Point Konfiguration (Fallback wenn Station fehlschlägt)
#define AP_SSID         "LIN-Proxy-AP"
#define AP_PASSWORD     "linproxy123"
#define AP_CHANNEL      6
#define AP_MAX_CONN     4

// Ethernet PHY Konfiguration (KSZ8081RNA)
#define ETH_PHY_ADDR    0
#define ETH_PHY_RST_GPIO -1
#define ETH_PHY_MDC     GPIO_NUM_23
#define ETH_PHY_MDIO    GPIO_NUM_18

// Logging Konfiguration
#define LOG_TO_CONSOLE  1    // 1=ESP_LOG aktiviert
#define LOG_TO_UDP      1    // 1=UDP Syslog aktiviert
#define SYSLOG_SERVER   "192.168.1.100"
#define SYSLOG_PORT     514

// LIN Frame Logging
#define LOG_LIN_FRAMES  1    // 1=Alle LIN-Frames loggen

#endif // CONFIG_H
