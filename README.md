# LIN Proxy für ESP32

Bidirektionaler LIN-Bus-Proxy mit Netzwerk-Logging und Web-Interface für ESP32 WROOM.

## 🚀 Übersicht

Dieser LIN-Proxy verbindet zwei LIN-Busse transparent und leitet alle Frames bidirektional weiter. Dabei regeneriert er LIN-Break-Signale korrekt und loggt alle Kommunikation über WiFi/Ethernet. Ein integriertes Web-Interface ermöglicht Firmware-Updates und System-Monitoring über den Browser.

**Haupt-Features:**
- 🔄 Transparente LIN-Bus-Weiterleitung (9600 Baud)
- 📡 WiFi/Ethernet mit AP-Fallback
- 🌐 Web-Interface für Updates & Monitoring
- 🔧 OTA Firmware-Updates (Web + Auto-Update)
- 📊 Live-Logging aller LIN-Frames (Console + UDP Syslog)

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

## ⚙️ Konfiguration

### Datei-Struktur

Das Projekt nutzt zwei Konfigurations-Dateien:

1. **`src/config_local.h`** - Lokale/Sensible Einstellungen (NICHT im Git!)
   - WiFi SSID & Passwort
   - Syslog-Server IP
   - OTA-Update URL
   - **Diese Datei selbst erstellen aus Template**

2. **`src/config.h`** - Allgemeine Einstellungen (im Git)
   - Feature-Flags (OTA aktiviert, Logging, etc.)
   - Hardware-Pins
   - Firmware-Version

### Setup lokale Konfiguration

**Beim ersten Mal:**
```bash
# Template kopieren
cp src/config_local.h.example src/config_local.h

# Anpassen
vim src/config_local.h
```

**Inhalt von `config_local.h`:**

**Inhalt von `config_local.h`:**

```c
// Netzwerk-Modus
#define USE_ETHERNET 0  // 0=WiFi, 1=Ethernet

// WiFi Station (Hauptverbindung)
#define WIFI_SSID       "MeinWLAN"      // <-- ANPASSEN
#define WIFI_PASSWORD   "MeinPasswort"  // <-- ANPASSEN

// WiFi Access Point (Fallback)
#define AP_SSID         "LIN-Proxy-AP"
#define AP_PASSWORD     "linproxy123"   // Min. 8 Zeichen

// Syslog Server (für Remote-Logging)
#define SYSLOG_SERVER   "192.168.1.100" // <-- ANPASSEN
#define SYSLOG_PORT     514

// OTA Update URL (für Auto-Update)
#define FW_UPDATE_URL   "http://192.168.1.100:8080/firmware.bin"
```

### Allgemeine Einstellungen (`config.h`)

Diese Werte können in **`src/config.h`** angepasst werden:

**Logging:**
```c
#define LOG_TO_CONSOLE  1   // ESP_LOG auf Serial Monitor
#define LOG_TO_UDP      1   // UDP Syslog aktiviert
#define LOG_LIN_FRAMES  1   // Alle LIN-Frames loggen
```

**OTA & Web-Interface:**
```c
#define FW_VERSION      "1.0.0"         // Firmware-Version
#define OTA_ENABLED     1               // OTA aktiviert
#define AUTO_UPDATE     1               // Automatischer Check
#define UPDATE_INTERVAL 3600            // Check-Intervall (Sekunden)
#define WEB_SERVER_ENABLED  1           // Web-Interface
#define WEB_SERVER_PORT     80          // HTTP-Port
```

**Hardware (Ethernet PHY):**
```c
#define ETH_PHY_ADDR    0
#define ETH_PHY_MDC     GPIO_NUM_23
#define ETH_PHY_MDIO    GPIO_NUM_18
```

### Warum zwei Dateien?

✅ **Sicherheit**: Sensible Daten (Passwörter) nicht ins Repository  
✅ **Team**: Jeder Entwickler hat eigene `config_local.h`  
✅ **CI/CD**: Build ohne sensible Daten im Code  
✅ **Fallback**: Wenn `config_local.h` fehlt, nutzt `config.h` Default-Werte

## Build & Flash

### Voraussetzungen
- **PlatformIO**: Empfohlenes Build-System (oder ESP-IDF 4.4.5+)
- **USB-Kabel**: Für Flash und Serial Monitor
- **Python 3**: Für PlatformIO und Tools

### Schritt 1: Repository klonen
```bash
git clone <repository-url>
cd lin_proxy
```

### Schritt 2: Konfiguration anpassen

**Lokale Einstellungen (sensible Daten):**
```bash
# Template kopieren
cp src/config_local.h.example src/config_local.h

# Mit Editor anpassen
vim src/config_local.h  # oder nano, VS Code, etc.
```

In **`src/config_local.h`** anpassen:
```c
// WiFi-Zugangsdaten
#define WIFI_SSID       "DeinWLAN"        // <-- ANPASSEN
#define WIFI_PASSWORD   "DeinPasswort"    // <-- ANPASSEN

// Access Point (Fallback)
#define AP_SSID         "LIN-Proxy-AP"
#define AP_PASSWORD     "linproxy123"

// Optional: Syslog-Server für Remote-Logging
#define SYSLOG_SERVER   "192.168.1.100"   // <-- ANPASSEN

// Optional: Auto-Update URL
#define FW_UPDATE_URL   "http://192.168.1.100:8080/firmware.bin"
```

**Wichtig:** 
- ✅ `config_local.h` wird **NICHT** ins Git committet (siehe `.gitignore`)
- ✅ `config_local.h.example` ist das Template im Repository
- ✅ Allgemeine Einstellungen bleiben in `src/config.h`

### Schritt 3: Build & Flash mit PlatformIO
```bash
# Projekt bauen
pio run

# ESP32 flashen und Serial Monitor starten
pio run -t upload -t monitor

# Alternativ: Nur flashen (ohne Monitor)
pio run -t upload
```

**Serial Monitor beenden**: `Ctrl+C`

### Alternative: Build mit ESP-IDF
```bash
# ESP-IDF Umgebung aktivieren
. $IDF_PATH/export.sh

# Bauen
idf.py build

# Flashen und Monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

### Build-Ausgaben
Nach erfolgreichem Build findest du:
- **Firmware Binary**: `.pio/build/esp32dev/firmware.bin`
- **Bootloader**: `.pio/build/esp32dev/bootloader.bin`
- **Memory Usage**: Im Build-Log (z.B. "Flash: 80.8%")

## 📖 Erste Inbetriebnahme

### 1. Hardware aufbauen
- ESP32 mit USB verbinden
- LIN-Transceiver (TJA1021) an Pins 12-15 anschließen
- Zwei LIN-Busse an LIN1 und LIN2 anschließen

### 2. Firmware flashen
```bash
cd lin_proxy
# Config anpassen (siehe oben)
vim src/config.h
# Flashen
pio run -t upload -t monitor
```

### 3. Serial Monitor beobachten
Nach dem Flash erscheint im Serial Monitor:
```
I (1234) LIN_PROXY: === LIN Proxy v1.0.0 ===
I (1235) NETWORK: WiFi gestartet: STA versucht Verbindung zu 'MeinWLAN', AP='LIN-Proxy-AP'
I (3456) NETWORK: WiFi Station IP: 192.168.1.123
I (3457) OTA: OTA initialisiert, Version: 1.0.0
I (3458) WEBSERVER: HTTP-Server gestartet auf Port 80
I (3459) LIN_PROXY: LIN proxy gestartet (9600 baud)
I (3460) LIN_PROXY: Web-Interface: http://<IP>:80
```

**Wichtig**: Notiere die IP-Adresse (z.B. `192.168.1.123`)!

### 4. Web-Interface öffnen
1. Öffne Browser und gehe zu `http://192.168.1.123` (deine ESP32-IP)
2. Du siehst das Web-Interface mit:
   - **System Info**: Firmware-Version, WiFi-Status
   - **Firmware Update**: Datei-Upload für neue Firmware
   - **Actions**: Reboot-Button

### 5. LIN-Frames beobachten
Im Serial Monitor erscheinen alle LIN-Frames:
```
I (5678) LIN_PROXY: [LIN1→LIN2] ID=0x3C Data=12 34 56 78 AB 
I (5789) LIN_PROXY: [LIN2→LIN1] ID=0x1A Data=FF EE DD CC 
```

**Log-Format**: `[Richtung] ID=<LIN-ID> Data=<Bytes in Hex>`

## 🔧 Verwendung

### Web-Interface Funktionen

**System Info anzeigen**
- Öffne `http://<ESP32-IP>`
- Zeigt: Firmware-Version, WiFi SSID, AP SSID

**Firmware-Update über Browser**
1. Baue neue Firmware: `pio run`
2. Öffne Web-Interface: `http://<ESP32-IP>`
3. Klicke "Choose File" und wähle `.pio/build/esp32dev/firmware.bin`
4. Klicke "Upload Firmware"
5. ESP32 installiert Update und bootet neu (ca. 30 Sekunden)
6. Seite neu laden → neue Version erscheint

**Update-Check**
- Klicke im Web-Interface auf "Check for Updates"
- Prüft ob neue Version auf `FW_UPDATE_URL` verfügbar ist
- Bei neuer Version: Download und Installation möglich

**Reboot**
- Klicke "Reboot ESP32" für Neustart
- ESP32 bootet in 2-3 Sekunden neu

### LIN-Frame-Logging

**Console-Logging** (Standard aktiv)
```bash
# Serial Monitor starten
pio device monitor -b 115200

# Logs zeigen LIN-Traffic:
[LIN1→LIN2] ID=0x3C Data=12 34 56 78 AB CD
[LIN2→LIN1] ID=0x1A Data=01 02 03
```

**UDP Syslog** (optional)
1. Aktiviere in `config.h`: `#define LOG_TO_UDP 1`
2. Setze Syslog-Server: `#define SYSLOG_SERVER "192.168.1.100"`
3. Auf Server:
   ```bash
   # Syslog empfangen (Linux/Mac)
   nc -lu 514
   ```

### WiFi-Modi

**Station-Modus** (Standard)
- ESP32 verbindet sich mit `WIFI_SSID`
- Erhält IP via DHCP
- Bei Verbindungsabbruch: Auto-Reconnect

**Access Point Fallback**
- Wenn Station fehlschlägt: ESP32 öffnet eigenen AP
- SSID: `AP_SSID` (Standard: "LIN-Proxy-AP")
- Passwort: `AP_PASSWORD`
- IP: `192.168.4.1` (Standard ESP32-AP-IP)
- Verbinde mit AP und öffne `http://192.168.4.1`

### Ethernet-Modus

Umschalten in [src/config.h](src/config.h):
```c
#define USE_ETHERNET 1  // Statt WiFi
```
- Nutzt KSZ8081RNA PHY
- DHCP für IP-Bezug
- Keine AP-Fallback-Option

## 🔄 OTA Firmware-Update

### Methode 1: Web-Interface (Empfohlen)

**Schritt-für-Schritt:**
1. **Neue Firmware bauen**:
   ```bash
   pio run
   # Firmware liegt nun in: .pio/build/esp32dev/firmware.bin
   ```

2. **Web-Interface öffnen**:
   - Browser: `http://<ESP32-IP>`
   
3. **Upload durchführen**:
   - Klicke "Choose File"
   - Wähle `.pio/build/esp32dev/firmware.bin`
   - Klicke "Upload Firmware"
   
4. **Warten**: Update dauert ca. 20-30 Sekunden
   - ESP32 zeigt "Uploading..." → "Update erfolgreich!"
   - Automatischer Reboot
   
5. **Verifizieren**: 
   - Seite neu laden
   - Neue Version wird angezeigt

**Troubleshooting**:
- Upload fehlschlägt? → Prüfe WiFi-Verbindung
- Browser timeout? → ESP32 Serial Monitor prüfen für Fehlermeldungen
- Alte Version nach Update? → Hard-Refresh im Browser (Ctrl+F5)

### Methode 2: Automatisches Update

**Setup:**
1. HTTP-Server starten (auf PC oder Server):
   ```bash
   cd .pio/build/esp32dev/
   python3 -m http.server 8080
   # Server läuft auf http://<PC-IP>:8080
   ```

2. Versions-Datei erstellen:
   ```bash
   echo "1.0.1" > firmware.bin.version
   ```

3. In [src/config.h](src/config.h) konfigurieren:
   ```c
   #define FW_VERSION      "1.0.0"              // Aktuelle Version
   #define FW_UPDATE_URL   "http://192.168.1.100:8080/firmware.bin"
   #define AUTO_UPDATE     1                    // Aktivieren
   #define UPDATE_INTERVAL 3600                 // Check alle 1h
   ```

4. Firmware flashen mit neuer Config

**Verhalten:**
- ESP32 prüft alle `UPDATE_INTERVAL` Sekunden auf neue Version
- Vergleicht `firmware.bin.version` mit `FW_VERSION`
- Bei Unterschied: automatischer Download und Installation
- Logs zeigen Update-Prozess

**Manueller Trigger** (ohne Auto-Update):
```c
#define AUTO_UPDATE 0  // Deaktivieren
```
Im Code kannst du dann manuell aufrufen:
```c
ota_update_from_url(FW_UPDATE_URL);  // In eigener Funktion
```

### Methode 3: PlatformIO OTA (Development)

**Für WiFi-basiertes Development OTA:**
```bash
# In platformio.ini aktivieren (experimentell):
upload_protocol = espota
upload_port = <ESP32-IP>

# Dann flashen ohne USB:
pio run -t upload
```

**Hinweis**: Aktuell nur Web-Interface und HTTP-OTA vollständig implementiert.

## 🛠️ Troubleshooting

### Build-Probleme

**`config_local.h not found`**
- **Ursache**: Template nicht kopiert
- **Lösung**: 
  ```bash
  cp src/config_local.h.example src/config_local.h
  ```

**`undefined reference to network_log`**
- **Ursache**: `network.c` nicht in CMakeLists.txt
- **Lösung**: In `src/CMakeLists.txt` prüfen:
  ```cmake
  idf_component_register(
      SRCS "lin_proxy.c" "network.c" "ota.c" "webserver.c"
      INCLUDE_DIRS "."
  )
  ```

**`MIN undeclared`**
- **Ursache**: Makro fehlt in webserver.c
- **Lösung**: Sollte bereits in Code sein, sonst clean build: `pio run -t clean && pio run`

**`esp_https_ota` API-Fehler**
- **Ursache**: ESP-IDF Version-Unterschiede
- **Lösung**: Verwende `esp_https_ota(&config)` statt `esp_https_ota(&ota_config)`

### WiFi-Probleme

**ESP32 verbindet nicht mit WiFi**
- Prüfe SSID/Passwort in `config.h`
- Serial Monitor zeigt: `WiFi Station getrennt, versuche Reconnect...`
- Nach mehreren Fehlversuchen: ESP32 startet AP-Modus
- Verbinde dann mit AP-SSID (Standard: "LIN-Proxy-AP")

**AP-Modus wird nicht gestartet**
- Prüfe: `AP_PASSWORD` muss mindestens 8 Zeichen haben
- Für offenes AP: `#define AP_PASSWORD ""` setzen

**IP-Adresse nicht sichtbar**
- Serial Monitor öffnen: `pio device monitor -b 115200`
- Warte auf Log: `WiFi Station IP: 192.168.x.x`
- Falls nicht erscheint: Router-DHCP prüfen

### OTA-Update-Probleme

**Upload fehlschlägt im Browser**
- Prüfe WiFi-Verbindung (ping ESP32-IP)
- Firmware-Größe prüfen: Max. ~1.5 MB für OTA-Partition
- Serial Monitor prüfen für Error-Logs

**Auto-Update startet nicht**
- Prüfe `AUTO_UPDATE 1` in config.h
- Prüfe `FW_UPDATE_URL` erreichbar (HTTP, nicht HTTPS für simple Config)
- Serial Monitor zeigt: `Prüfe auf Firmware-Updates...`

**Version wird nicht erkannt**
- Erstelle `firmware.bin.version` auf Server
- Format: Eine Zeile mit Version, z.B. "1.0.1"
- Keine Leerzeichen oder zusätzliche Zeilen

### LIN-Bus-Probleme

**Keine LIN-Frames im Log**
- Prüfe `LOG_LIN_FRAMES 1` in config.h
- Prüfe LIN-Transceiver-Verkabelung (Pins 12-15)
- LIN-Bus muss aktiv Frames senden
- Break-Detection braucht korrekte Frame-Timing

**Frames korrupt/unvollständig**
- LIN-Baudrate prüfen (fest 9600)
- TJA1021-Transceiver auf 3.3V-Kompatibilität prüfen
- Masse zwischen ESP32 und LIN-Bus verbinden

### Web-Interface-Probleme

**Seite lädt nicht**
- IP im Browser korrekt? (aus Serial Monitor)
- Port 80 offen? (Standard HTTP)
- Firewall deaktivieren zum Testen
- Anderes Gerät im gleichen Netzwerk versuchen

**Upload-Button funktioniert nicht**
- JavaScript im Browser aktiviert?
- Moderne Browser nutzen (Chrome, Firefox, Edge)
- Browser-Console öffnen (F12) für Fehler

### Weitere Hilfe

- **Serial Monitor Logs**: Immer zuerst prüfen: `pio device monitor -b 115200`
- **ESP32 Reboot**: Oft hilft Reboot nach Config-Änderung
- **Clean Build**: Bei merkwürdigen Fehlern: `pio run -t clean && pio run`
- **Issue melden**: Bei Bugs bitte Serial Monitor Log und config.h mitschicken

## 📁 Projektstruktur

```
lin_proxy/
├── src/                        # Quellcode
│   ├── config.h               # Allgemeine Konfiguration (im Git)
│   ├── config_local.h         # Lokale Einstellungen (NICHT im Git!)
│   ├── config_local.h.example # Template für config_local.h
│   ├── lin_proxy.c            # Haupt-LIN-Proxy-Logik
│   ├── network.c/h            # WiFi/Ethernet-Initialisierung
│   ├── ota.c/h                # OTA-Update-Funktionen
│   ├── webserver.c/h          # HTTP-Server & Web-Interface
│   └── CMakeLists.txt         # ESP-IDF Build-Config
├── .pio/                      # PlatformIO Build-Dateien
│   └── build/esp32dev/
│       └── firmware.bin       # Fertige Firmware für OTA
├── .gitignore                 # Git-Ignore (config_local.h!)
├── platformio.ini             # PlatformIO Projekt-Config
├── CMakeLists.txt             # Top-Level ESP-IDF CMake
└── README.md                  # Diese Datei
```

**Wichtigste Dateien:**
- **[src/config_local.h](src/config_local.h)**: Sensible Einstellungen (WiFi, Passwörter) - **NICHT im Git!**
- **[src/config.h](src/config.h)**: Feature-Flags und Hardware-Config
- **[src/lin_proxy.c](src/lin_proxy.c)**: LIN-State-Machine und Proxy-Logik
- **[platformio.ini](platformio.ini)**: Build-System-Konfiguration

## 🔧 Entwicklung & Architektur

### Code-Struktur

**LIN-Proxy-Kern** ([src/lin_proxy.c](src/lin_proxy.c)):
- **State-Machine**: 5 Zustände für LIN-Frame-Parsing
  - `IDLE` → `GOT_BREAK` → `GOT_SYNC` → `GOT_ID` → `DATA`
- **Break-Generierung**: GPIO-Workaround für LIN-Break (1500μs low)
- **Bidirektionale Tasks**: Zwei FreeRTOS-Tasks (LIN1→LIN2, LIN2→LIN1)
- **Frame-Buffering**: Komplettes Frame wird geloggt

**Netzwerk** ([src/network.c](src/network.c)):
- WiFi Station + AP-Fallback oder Ethernet
- UDP Syslog-Client für Remote-Logging
- Event-Handler für Verbindungs-Monitoring

**OTA** ([src/ota.c](src/ota.c)):
- HTTP-basiertes Firmware-Download
- Versions-Check gegen Remote-Server
- Auto-Update-Task (optional)

**Web-Interface** ([src/webserver.c](src/webserver.c)):
- HTTP-Server (ESP-IDF `esp_http_server`)
- Embedded HTML mit JavaScript
- Multipart-Upload für Firmware-Binary

### LIN-Protokoll-Details

**Warum GPIO-Break?**
- ESP32 UART kann keinen LIN-Break senden (13+ bit dominant)
- Workaround: TX-Pin als GPIO low schalten für 1500μs
- Danach zurück auf UART-Modus

**Frame-Weiterleitung:**
1. Input-UART empfängt Break → State: `GOT_BREAK`
2. Sync-Byte `0x55` → State: `GOT_SYNC`
3. ID-Byte (mit Parität) → Break regenerieren auf Output-UART → State: `GOT_ID`
4. Daten-Bytes transparent weiterleiten → State: `DATA`
5. Nächster Break: Frame loggen, State zurück auf `IDLE`

**Baudrate:** Fest 9600 Baud (LIN-Standard für Low-Speed)

### Hardware-Anforderungen

**LIN-Transceiver:**
- **Empfohlen**: TJA1021 (3.3V I/O-kompatibel)
- **Wichtig**: Manche TJA1021-Varianten sind nur 5V → Level-Shifter nötig
- **Pins**: RX/TX an ESP32, LIN an Bus, GND gemeinsam

**Ethernet PHY (optional):**
- KSZ8081RNA benötigt 25 MHz Clock auf GPIO16
- Code: `gpio_set_drive_capability(GPIO_NUM_16, GPIO_DRIVE_CAP_3)`
- PHY-Register: `eth->phy_reg_write(eth, addr, 0x1f, 0x80)`

### Build-System

**PlatformIO** (empfohlen):
- Nutzt ESP-IDF 4.4.5 im Hintergrund
- Automatische Toolchain-Installation
- `platformio.ini` definiert Board und Optionen

**ESP-IDF nativ**:
- `src/CMakeLists.txt` registriert alle `.c` Dateien
- `idf.py menuconfig` für erweiterte Optionen
- Für Produktions-Builds mit Custom-Partitions

### Weitere Entwickler-Infos

Detaillierte Architektur-Dokumentation in:
**[.github/copilot-instructions.md](.github/copilot-instructions.md)**

Enthält:
- Pin-Belegungen und Hardware-Details
- LIN-Protokoll-Grundlagen
- FreeRTOS Task-Patterns
- Debugging-Tipps
- Häufige Fallstricke

## Lizenz

[Lizenz hinzufügen]
