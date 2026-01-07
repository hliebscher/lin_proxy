#!/usr/bin/env python3
"""
Einfacher Syslog-Server für LIN-Proxy
Empfängt UDP-Syslog-Nachrichten auf Port 514 und schreibt sie in eine Datei.
"""

import socket
import datetime
import sys
import os
from pathlib import Path

# Konfiguration
SYSLOG_PORT = 514
SYSLOG_HOST = "0.0.0.0"  # Lauscht auf allen Interfaces
LOG_FILE = "lin_proxy_syslog.log"
MAX_PACKET_SIZE = 4096

def main():
    # Log-Datei öffnen (append mode)
    log_path = Path(LOG_FILE)
    print(f"Syslog-Server gestartet")
    print(f"Lauscht auf {SYSLOG_HOST}:{SYSLOG_PORT}")
    print(f"Schreibt in: {log_path.absolute()}")
    print(f"Drücke Ctrl+C zum Beenden\n")
    
    # UDP-Socket erstellen
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        sock.bind((SYSLOG_HOST, SYSLOG_PORT))
    except PermissionError:
        print(f"FEHLER: Port {SYSLOG_PORT} benötigt Root-Rechte!")
        print(f"Starte mit: sudo python3 {sys.argv[0]}")
        print(f"ODER nutze Port >1024 und passe ESP32 config_local.h an (SYSLOG_PORT)")
        sys.exit(1)
    except OSError as e:
        print(f"FEHLER: Kann nicht auf Port {SYSLOG_PORT} binden: {e}")
        sys.exit(1)
    
    with open(log_path, 'a', encoding='utf-8') as log_file:
        try:
            while True:
                # Empfange Daten
                data, addr = sock.recvfrom(MAX_PACKET_SIZE)
                timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                
                try:
                    message = data.decode('utf-8').strip()
                except UnicodeDecodeError:
                    message = data.decode('latin-1').strip()
                
                # Formatiere Log-Zeile
                log_line = f"[{timestamp}] {addr[0]}:{addr[1]} | {message}"
                
                # Schreibe in Datei und auf Konsole
                print(log_line)
                log_file.write(log_line + '\n')
                log_file.flush()  # Sofort auf Disk schreiben
                
        except KeyboardInterrupt:
            print("\n\nServer wird beendet...")
        finally:
            sock.close()
            print(f"Log-Datei: {log_path.absolute()}")

if __name__ == "__main__":
    main()
