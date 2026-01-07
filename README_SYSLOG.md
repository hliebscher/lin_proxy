# Syslog-Server für LIN-Proxy

Einfacher Python-basierter Syslog-Server zum Empfangen und Loggen von UDP-Nachrichten vom ESP32 LIN-Proxy.

## Features

- ✅ Empfängt UDP-Syslog auf Port 514 (Standard)
- ✅ Schreibt alle Nachrichten in `lin_proxy_syslog.log`
- ✅ Zeigt Nachrichten live auf der Konsole
- ✅ Timestamps für jede Nachricht
- ✅ Zeigt Absender-IP und Port an

## Installation

Keine zusätzlichen Pakete nötig – nutzt nur Python Standard-Library.

```bash
# Script ausführbar machen
chmod +x syslog_server.py
```

## Verwendung

### Option 1: Mit Root-Rechten (Port 514)

```bash
sudo python3 syslog_server.py
```

**Hinweis:** Port 514 ist ein privilegierter Port (<1024) und benötigt `sudo`.

### Option 2: Unprivilegierter Port (empfohlen für Mac)

Wenn du `sudo` vermeiden möchtest:

1. **Ändere den Port im Script:**
   ```python
   SYSLOG_PORT = 5514  # Statt 514
   ```

2. **Passe die ESP32-Konfiguration an:**
   In `src/config_local.h`:
   ```c
   #define SYSLOG_SERVER   "192.168.4.1"  // Deine Mac IP
   #define SYSLOG_PORT     5514           // Geänderter Port
   ```

3. **Starte den Server:**
   ```bash
   python3 syslog_server.py
   ```

## ESP32-Konfiguration

Stelle sicher, dass in `src/config_local.h` die richtige IP eingetragen ist:

```c
// IP deines Mac im lokalen Netzwerk
#define SYSLOG_SERVER   "192.168.1.100"  // <-- ANPASSEN
#define SYSLOG_PORT     514
```

### Mac IP-Adresse herausfinden:

```bash
# WiFi
ipconfig getifaddr en0

# Ethernet
ipconfig getifaddr en1
```

Oder wenn ESP32 im AP-Modus ist:
- ESP32 AP IP: `192.168.4.1`
- Mac (als Client): `192.168.4.x` (erhält DHCP vom ESP32)
- Nutze: `ifconfig` um die IP auf dem `bridge`-Interface zu finden

## Output-Beispiel

```
Syslog-Server gestartet
Lauscht auf 0.0.0.0:514
Schreibt in: /Users/hliebscher/github/lin_proxy/lin_proxy_syslog.log
Drücke Ctrl+C zum Beenden

[2026-01-07 14:32:15.234] 192.168.4.59:51234 | [LIN_PROXY] LIN proxy gestartet (9600 baud)
[2026-01-07 14:32:16.128] 192.168.4.59:51234 | [LIN1→LIN2] ID=0x3C Data=01 02 03 04 05 06 07 08
[2026-01-07 14:32:16.342] 192.168.4.59:51234 | [LIN2→LIN1] ID=0x2D Data=FF 00 AA BB
```

## Log-Datei

Alle Nachrichten werden in `lin_proxy_syslog.log` gespeichert (append mode):
- Datei wird automatisch beim ersten Start erstellt
- Neue Nachrichten werden angehängt (nicht überschrieben)
- Kann mit jedem Texteditor geöffnet werden

```bash
# Live-Anzeige der letzten Zeilen
tail -f lin_proxy_syslog.log

# Log durchsuchen
grep "LIN1→LIN2" lin_proxy_syslog.log

# Log löschen (falls zu groß)
rm lin_proxy_syslog.log
```

## Troubleshooting

### "FEHLER: Port 514 benötigt Root-Rechte"

→ Nutze `sudo` oder ändere Port auf >1024 (siehe Option 2)

### "Keine Nachrichten empfangen"

1. **Firewall prüfen:**
   ```bash
   # macOS Firewall-Status
   /usr/libexec/ApplicationFirewall/socketfilterfw --getglobalstate
   ```
   Falls aktiviert: Python in Firewall-Einstellungen erlauben

2. **ESP32 IP prüfen:**
   - Schau im ESP32-Log nach der zugewiesenen IP
   - Ping vom Mac: `ping <ESP32-IP>`

3. **UDP-Port testen:**
   ```bash
   # Test-Nachricht vom Mac senden
   echo "Test" | nc -u -w1 localhost 514
   ```

4. **ESP32 LOG_TO_UDP aktiviert?**
   In `src/config.h` muss sein:
   ```c
   #define LOG_TO_UDP      1
   ```

### "Connection refused" oder "Address already in use"

→ Ein anderer Prozess nutzt bereits Port 514:
```bash
# Prüfe welcher Prozess Port 514 nutzt
sudo lsof -i :514

# Oder nutze einen anderen Port (siehe Option 2)
```

## Beenden

Drücke `Ctrl+C` um den Server zu stoppen.
