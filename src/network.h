#ifndef NETWORK_H
#define NETWORK_H

#include "esp_err.h"

// Netzwerk initialisieren (WiFi oder Ethernet)
esp_err_t network_init(void);

// UDP-Log-Nachricht senden (für Syslog)
void network_log(const char *msg);

// Aktuelle IP-Adresse als String (statischer Buffer)
char* network_get_ip_string(void);

#endif // NETWORK_H
