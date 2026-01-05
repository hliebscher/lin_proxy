# LIN Proxy für ESP32

Bidirektionaler LIN-Bus-Proxy mit Netzwerk-Logging (WiFi/Ethernet) für ESP32 WROOM.

## Features

- **Transparenter LIN-Proxy**: Verbindet zwei LIN-Busse (9600 Baud) mit vollständiger Frame-Regenerierung
- **Netzwerk-Logging**: Logs über WiFi oder Ethernet
  - WiFi mit Fallback auf Access-Point-Modus
  - Ethernet via KSZ8081RNA PHY
- **ESP-IDF-basiert**: Professioneller ESP32-Entwicklungsstack

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

Netzwerk-Interface per `idf.py menuconfig` wählbar:
- WiFi (Station oder Access Point)
- Ethernet (KSZ8081RNA PHY)

PHY-Konfiguration erfolgt via: `eth->phy_reg_write(eth, ksz80xx->addr, 0x1f, 0x80)`

## Build & Flash

```bash
# ESP-IDF Umgebung aktivieren
. $IDF_PATH/export.sh

# Konfigurieren (Netzwerk-Optionen)
idf.py menuconfig

# Bauen und Flashen
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Entwicklung

Siehe [.github/copilot-instructions.md](.github/copilot-instructions.md) für detaillierte Architektur- und Entwicklungshinweise.

## Lizenz

[Lizenz hinzufügen]
