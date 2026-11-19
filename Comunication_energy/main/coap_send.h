#pragma once
#include <stdbool.h>

// Prepares CoAP for the server in secrets.h (COAP_SERVER_URI).
bool coap_client_start(void);

// Sends text as a CoAP POST.
//   confirmable = true : CON, waits up to timeout_ms for the server's reply -> true if it came
//   confirmable = false: NON with the No-Response option -> true if it was handed to the network
bool coap_client_send(const char *text, bool confirmable, int timeout_ms);

// Lets libcoap handle anything pending (late replies, retransmissions) without waiting.
void coap_client_poll(void);
