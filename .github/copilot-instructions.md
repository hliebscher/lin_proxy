# Copilot Instructions für lin_proxy

## Projektübersicht

Dies ist ein **ESP-IDF LIN-Proxy-Projekt** für ESP32, das zwei LIN-Busse (LIN1 und LIN2) bidirektional verbindet und LIN-Protokoll-Frames weiterleitet.

**Zweck**: Transparenter Proxy für LIN-Bus-Kommunikation, nützlich für Monitoring, Debugging oder Protokoll-Translation zwischen zwei LIN-Netzwerken.

**Projekt-Name**: `womolin_proxy` (CMakeLists.txt) - minimalistisches Single-File-Projekt ohne externe Dependencies

### LIN-Protokoll Grundlagen
- **LIN-Frame-Struktur**: BREAK (≥13 Bitzeiten dominant low) → SYNC (0x55) → ID (mit Parität) → Daten → Checksumme
- **Kein UART-basiertes Break**: Standard `uart_write_bytes()` kann keinen LIN-Break erzeugen → GPIO-Workaround nötig
- **UART-Konfiguration**: 8N1 ohne Hardware-Parität (LIN-Parität ist in ID-Byte kodiert, nicht UART-Parität)
- **Timing kritisch**: Bei 9600 baud = 104µs/Bit; Slave-Response-Zeitfenster sind definiert

## Architektur

### Kern-Komponenten
- **Dual-UART-Setup**: UART1 (LIN1: GPIO14/15) und UART2 (LIN2: GPIO13/12) mit 9600 Baud
- **FreeRTOS-Proxy-Tasks**: Zwei symmetrische Tasks (`lin1_to_lin2`, `lin2_to_lin1`) mit Priorität 12
- **LIN-State-Machine**: 5-Zustands-FSM (IDLE → GOT_BREAK → GOT_SYNC → GOT_ID → DATA) für LIN-Frame-Parsing
- **GPIO-Break-Generation**: Software-gesteuerte Break-Generierung via GPIO (1500μs low) für LIN-Header

### Datenfluss
1. UART-Events (Break/Data) über FreeRTOS-Queues empfangen
2. State-Machine parst LIN-Header (Break + 0x55 Sync + ID)
3. Bei vollständigem Header: Break+Sync+ID neu generieren auf Ausgangs-UART
4. Daten-Bytes transparent weiterleiten

## Build & Entwicklung

### Build-System
- **ESP-IDF CMake**: Nutze `idf.py build` für Builds, nicht Standard-CMake
- **Komponenten-Struktur**: `main/CMakeLists.txt` registriert via `idf_component_register()`
- **IDF_PATH erforderlich**: Umgebungsvariable muss auf ESP-IDF zeigen

### Typische Kommandos
```bash
idf.py build            # Projekt bauen
idf.py -p PORT flash    # Flashen auf ESP32
idf.py monitor          # Serieller Monitor (Logs)
idf.py menuconfig       # Konfiguration anpassen
idf.py -p PORT flash monitor  # Flash + Monitor kombiniert
```

### Projektstruktur
- Keine `sdkconfig` im Repo → Default ESP-IDF-Konfiguration wird beim ersten Build generiert
- Single-File-Implementierung: gesamte Logik in [main/lin_proxy.c](main/lin_proxy.c)
- Keine externen Komponenten oder Libraries benötigt (nur ESP-IDF-Core)

## Code-Konventionen

### Hardware-Definitionen
- Pins als `GPIO_NUM_x` definieren (siehe LIN1_TX, LIN2_RX etc.)
- UARTs als `UART_NUM_x` aus ESP-IDF-Enums nutzen
- Buffer-Größen als Defines (`UART_BUF 2048`)

### FreeRTOS-Patterns
- **Task-Prioritäten**: Proxy-Tasks nutzen Priorität 12
- **Stack-Größe**: 4096 Bytes für UART-Handler-Tasks @ 9600 = 1.35ms, robust)
  - **Warum GPIO**: ESP32 UART kann keinen Break senden → TX-Pin kurz als GPIO-Output low schalten
  - Implementierung: `gpio_set_direction()` → `gpio_set_level(0)` → `delay_us()` → `gpio_set_direction(INPUT_OUTPUT)`
- **Timeout**: `uart_set_rx_timeout(uart, 2)` für Frame-Erkennung
- **Frame-basiert statt Blind-Streaming**: Header (Break+Sync+ID) wird erkannt, neu erzeugt und dann Daten weitergeleitet

### Hardware-Anforderungen
- **LIN-Transceiver**: TJA1021 oder ähnlich für 12V LIN-Bus ↔ 3.3V ESP32-Logik
- **WICHTIG**: TJA1021-Variante muss 3.3V-kompatibel sein (VIO/Logikversorgung prüfen!)
  - Manche Varianten sind nur 5V → Level-Shifter erforderlich oder ESP32-Pins gefährdet
- **Bus-Topologie**: Beide LIN-Segmente brauchen separate LIN-Beschaltung (Pullup/Termination gemäß LIN-Spec)
- **Isolierung**: Proxy trennt physikalisch zwei LIN-Busse → verhindert Master-KollisionenAY`

### LIN-Protokoll-Spezifika
- **Break-Detection**: `UART_BREAK` oder `UART_FRAME_ERR` Events als Break behandeln
- **Sync-Byte**: Immer `0x55` nach Break erwarten
- **Break-Generation**: GPIO-low für 1500μs (länger als Standard 13-Bit, robust für 9600 Baud)
- **Timeout**: `uart_set_rx_timeout(uart, 2)` für Frame-Erkennung

### Logging
- `ESP_LOGI(TAG, ...)` für Info-Logs nutzen
- TAG immer als `#define TAG "LIN_PROXY"` definieren

- **Latenz-Probleme**: Wenn Slaves zu spät antworten, prüfe Task-Prioritäten und Queue-Delays

## Häufige Fallstricke
- ❌ **Falscher TJA1021-Typ**: 5V-Logik statt 3.3V → ESP32-Pins können beschädigt werden
- ❌ **Break nicht erkannt/erzeugt**: Bus erscheint "tot" → Frame-Errors prüfen, GPIO-Timing validieren
- ❌ **Zu langsamer Proxy**: Slave-Response außerhalb Zeitfenster → Task-Priorität erhöhen, Code optimieren
- ❌ **Bus nicht getrennt**: Zwei Master auf einem physikalischen Bus → Buskollisionen und Corruption
- ❌ **UART-Parität aktiviert**: LIN nutzt 8N1; Parität ist in ID-Byte, nicht UART-Parität
- ❌ **Blindes Byte-Streaming**: Ohne Break-Regenerierung können Slaves Header nicht erkennen

## Mögliche Erweiterungen
- **Filtering**: Bestimmte IDs blocken oder modifizieren in State-Machine
- **Checksumme-Neuberechnung**: Bei Payload-Änderungen classic/enhanced Checksum anpassen
- **Logging/Sniffing**: Frames auf SD-Karte oder UART3 ausgeben für Analyse
- **Rate-Limiting**: Flood-Protection bei fehlerhaften Slaves
## Wichtige Dateien
- [main/lin_proxy.c](main/lin_proxy.c): Gesamte Proxy-Logik, State-Machine, UART-Setup
- [main/CMakeLists.txt](main/CMakeLists.txt): Komponenten-Registrierung für ESP-IDF
- [CMakeLists.txt](CMakeLists.txt): Top-Level ESP-IDF-Projekt (nutzt `$ENV{IDF_PATH}`)

## Debugging-Tipps
- **Monitor-Ausgabe**: `idf.py monitor` zeigt ESP_LOG-Ausgaben und Panics
- **UART-Events debuggen**: Temporär Event-Types loggen vor State-Machine
- **Break-Timing**: Bei Problemen mit Break-Detection `us_low` in `lin_send_break_gpio()` anpassen
- **Pin-Konflikte**: Prüfe mit `gpio_dump_io_configuration()` bei unerwarteten Pin-States
