#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// HAUPT-KONFIGURATION
// ============================================================================
// Lokale Einstellungen (SSID, Passwort, etc.) in config_local.h anpassen!
// 
// SETUP:
// 1. Kopiere src/config_local.h.example zu src/config_local.h
// 2. Passe Netzwerk-Einstellungen in config_local.h an
// 3. config_local.h wird NICHT ins Git committet (siehe .gitignore)

// Inkludiere lokale Einstellungen (WiFi, Syslog, etc.)
// Korrekte Nutzung von __has_include: direkt als Ausdruck in #if verwenden
#if __has_include("config_local.h")
#  include "config_local.h"
#  define HAS_LOCAL_CONFIG 1
#endif

// Fallback-Werte wenn config_local.h fehlt
#ifndef HAS_LOCAL_CONFIG
    #warning "config_local.h nicht gefunden! Nutze Default-Werte. Kopiere config_local.h.example zu config_local.h"
    
    #define USE_ETHERNET 0
    #define WIFI_SSID       "MeinWLAN"
    #define WIFI_PASSWORD   "MeinPasswort"
    #define AP_SSID         "LIN-Proxy-AP"
    #define AP_PASSWORD     "linproxy123"
    #define AP_CHANNEL      6
    #define AP_MAX_CONN     4
    #define SYSLOG_SERVER   "192.168.1.100"
    #define SYSLOG_PORT     514
    #define FW_UPDATE_URL   "http://192.168.1.100:8080/firmware.bin"
#endif

// Ethernet PHY Konfiguration (KSZ8081RNA)
#define ETH_PHY_ADDR    0
#define ETH_PHY_RST_GPIO -1
#define ETH_PHY_MDC     GPIO_NUM_23
#define ETH_PHY_MDIO    GPIO_NUM_18

// Logging Konfiguration
#define LOG_TO_CONSOLE  1    // 1=ESP_LOG aktiviert
#define LOG_TO_UDP      1    // 1=UDP Syslog aktiviert

// LIN Frame Logging
#define LOG_LIN_FRAMES  1    // 1=Alle LIN-Frames loggen

// LIN Sniffer Modus (nur für Testing/Debugging)
#define LIN_SNIFFER_MODE 0   // 1=Aktiviert Sniffer auf LIN1 (deaktiviert Proxy!)
#define SNIFFER_DETAIL_LOGS 1 // 1=Detaillierte Frame-Analyse mit Timing

// Firmware Version
#define FW_VERSION      "1.0.0"

// OTA Update Konfiguration
#define OTA_ENABLED     1    // 1=OTA über HTTP aktiviert
#define AUTO_UPDATE     1    // 1=Automatischer Versions-Check und Update
#define UPDATE_INTERVAL 3600 // Auto-Update-Check alle 3600 Sekunden (1 Stunde)

// Web-Interface
#define WEB_SERVER_ENABLED  1   // 1=HTTP-Server für Web-Interface
#define WEB_SERVER_PORT     80  // HTTP-Port für Web-Interface

#endif // CONFIG_H

