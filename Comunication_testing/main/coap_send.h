#pragma once
#include <stdbool.h>

// Prepares CoAP for the server in secrets.h (COAP_SERVER_URI).
bool coap_client_start(void);


bool coap_client_send(const char *text, bool confirmable);