# LIN Proxy für ESP32

Bidirektionaler LIN-Bus-Proxy mit Netzwerk-Logging (WiFi/Ethernet) für ESP32 WROOM.

## Features

- **Transparenter LIN-Proxy**: Verbindet zwei LIN-Busse (9600 Baud) mit vollständiger Frame-Regenerierung
  - Break-Detection und Break-Regenerierung via GPIO
  - LIN-State-Machine mit 5 Zuständen (IDLE → BREAK → SYNC → ID → DATA)
  - Frame-Buffering für komplettes Logging
- **Netzwerk-Logging**: Remote-Logging über WiFi oder Ethernet
  - **WiFi Station-Modus** mit automatischem Fallback auf Access Point
  - **Ethernet** via KSZ8081RNA PHY (25 MHz Clock)
  - **UDP Syslog** für Remote-Log-Server
  - Logging aller LIN-Frames von beiden Ports (LIN1→LIN2 und LIN2→LIN1)
- **ESP-IDF 4.4.5** & **PlatformIO** kompatibel

## Hardware

- **MCU**: ESP32 WROOM mit 16 MB Flash-Speicher
- **WiFi**: Externe Antenne (UFL-Stecker, im Lieferumfang)
- **Ethernet**: KSZ8081RNA PHY mit 25 MHz Takt (über IO16)
- **LIN-Transceiver**: TJA1021 oder kompatibel (3.3V-Logik erforderlich!)

### Pin-Belegung

| GPIO | Funktion |
|------|----------|
| 12   | TXD2 LIN2 |
| 13   | RXD2 LIN2 |
| 14   | RXD1 LIN1 |
| 15   | TXD1 LIN1 |
| 16   | ETH CLK (25 MHz für PHY) |

**Hinweis**: Standard UART-Pins IO16/17 werden vom Ethernet PHY verwendet, daher wurden LIN-Pins auf IO12-15 umgelegt.

## Konfiguration

Alle Einstellungen in [src/config.h](src/config.h):

### Netzwerk-Modus
```c
#define USE_ETHERNET 0  // 0=WiFi, 1=Ethernet
```

### WiFi-Einstellungen
```c
// WiFi Station (Hauptverbindung)
#define WIFI_SSID       "MeinWLAN"
#define WIFI_PASSWORD   "MeinPasswort"

// WiFi Access Point (Fallback)
#define AP_SSID         "LIN-Proxy-AP"
#define AP_PASSWORD     "linproxy123"
#define AP_CHANNEL      6
#define AP_MAX_CONN     4
```

### Logging-Konfiguration
```c
#define LOG_TO_CONSOLE  1    // ESP_LOG auf Serial
#define LOG_TO_UDP      1    // UDP Syslog aktiviert
#define SYSLOG_SERVER   "192.168.1.100"
#define SYSLOG_PORT     514
#define LOG_LIN_FRAMES  1    // Alle LIN-Frames loggen
```

**Log-Format**: `[LIN1→LIN2] ID=0x3C Data=12 34 56 78 AB`

## Build & Flash

### Mit PlatformIO (empfohlen)
```bash
# Konfiguration anpassen
# Editiere src/config.h für WiFi/Ethernet/Logging

# Bauen
pio run

# Flashen und Monitor
pio run -t upload -t monitor
```

### Mit ESP-IDF
```bash
# ESP-IDF Umgebung aktivieren
. $IDF_PATH/export.sh

# Bauen und Flashen
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Erste Inbetriebnahme
1. **Konfiguration anpassen**: Editiere [src/config.h](src/config.h)
   - WiFi-Credentials für Station-Modus
   - Access-Point-Name (Fallback)
   - Syslog-Server IP für Remote-Logging
   - Netzwerk-Modus (WiFi/Ethernet)
2. **Build & Flash**: `pio run -t upload -t monitor`
3. **WiFi-Verbindung**: ESP32 versucht zuerst Station-Verbindung, startet bei Fehler AP-Modus
4. **Log-Monitoring**: LIN-Frames erscheinen im Serial Monitor und auf Syslog-Server

## Entwicklung

Siehe [.github/copilot-instructions.md](.github/copilot-instructions.md) für detaillierte Architektur- und Entwicklungshinweise.

## Lizenz

[Lizenz hinzufügen]
