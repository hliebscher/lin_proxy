#ifndef WEBSERVER_H
#define WEBSERVER_H

#include "esp_err.h"

// Web-Server initialisieren
esp_err_t webserver_init(void);

// Web-Server stoppen
void webserver_stop(void);

#endif // WEBSERVER_H
