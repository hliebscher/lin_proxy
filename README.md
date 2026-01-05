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
- **OTA Firmware-Updates**: 
  - **Web-Interface** für Firmware-Upload über Browser
  - **Auto-Update** mit Versions-Check (konfigurierbar)
  - **HTTP OTA** für Remote-Updates
- **Web-Interface**: 
  - System-Info und Status-Monitoring
  - Firmware-Upload direkt im Browser
  - Remote-Reboot
  - Zugriff über WiFi/Ethernet IP
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

### OTA & Web-Interface
```c
#define FW_VERSION      "1.0.0"
#define FW_UPDATE_URL   "http://192.168.1.100:8080/firmware.bin"

#define OTA_ENABLED     1    // OTA über HTTP aktiviert
#define AUTO_UPDATE     1    // Automatischer Versions-Check
#define UPDATE_INTERVAL 3600 // Auto-Update alle 3600 Sekunden (1 Stunde)

#define WEB_SERVER_ENABLED  1   // HTTP-Server für Web-Interface
#define WEB_SERVER_PORT     80  // Web-Interface Port
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
   - Firmware-Version und Update-URL
2. **Build & Flash**: `pio run -t upload -t monitor`
3. **WiFi-Verbindung**: ESP32 versucht zuerst Station-Verbindung, startet bei Fehler AP-Modus
4. **Web-Interface öffnen**: 
   - Verbinde mit WiFi (Station oder AP)
   - Öffne Browser: `http://<ESP32-IP>` (IP erscheint im Serial Monitor)
   - Funktionen: Firmware-Upload, System-Info, Reboot
5. **Log-Monitoring**: LIN-Frames erscheinen im Serial Monitor und auf Syslog-Server

### OTA Firmware-Update

**Variante 1: Web-Interface (empfohlen)**
1. Öffne `http://<ESP32-IP>` im Browser
2. Klicke "Choose File" und wähle `.pio/build/esp32dev/firmware.bin`
3. Klicke "Upload Firmware"
4. ESP32 bootet automatisch mit neuer Firmware

**Variante 2: Automatisches Update**
1. Aktiviere in [src/config.h](src/config.h): `#define AUTO_UPDATE 1`
2. Setze `FW_UPDATE_URL` auf HTTP-Server mit `firmware.bin`
3. Erstelle `firmware.bin.version` mit neuer Version-String (z.B. "1.0.1")
4. ESP32 prüft alle `UPDATE_INTERVAL` Sekunden auf neue Version
5. Bei neuer Version: automatischer Download und Reboot

**Variante 3: Manueller HTTP-Trigger**
```bash
# Firmware auf HTTP-Server bereitstellen
python3 -m http.server 8080

# ESP32 triggert Update über Serial oder API
```

## Entwicklung

Siehe [.github/copilot-instructions.md](.github/copilot-instructions.md) für detaillierte Architektur- und Entwicklungshinweise.

## Lizenz

[Lizenz hinzufügen]
