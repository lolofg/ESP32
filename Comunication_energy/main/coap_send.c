// coap_send.c - CoAP client (libcoap), based on Comunication_testing/main/coap_send.c
//
// Differences to the test version:
//   - the wait for the reply has a timeout parameter and uses the real time
//   - NON messages can carry the No-Response option (RFC 7967), so the server
//     sends nothing back - the CoAP counterpart of MQTT QoS 0
//   - libcoap's own log output is turned down (no printing during measurements)
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "coap3/coap.h"
#include "coap_send.h"
#include "secrets.h"

static const char *TAG = "coap";
static coap_context_t *ctx = NULL;          // libcoap's "workspace"
static coap_session_t *session = NULL;      // the "connection" to the server (UDP)
static coap_optlist_t *options_con = NULL;  // Uri-Path + Content-Format
static coap_optlist_t *options_non = NULL;  // the same + No-Response
static volatile bool got_reply = false;

// Called by libcoap when the server answers
static coap_response_t on_reply(coap_session_t *s, const coap_pdu_t *sent,
                                const coap_pdu_t *received, const coap_mid_t mid)
{
    got_reply = true;
    return COAP_RESPONSE_OK;
}

// Uri-Path ("/hello") and Content-Format (JSON) for one option list
static bool build_options(const coap_uri_t *uri, const coap_address_t *server,
                          coap_optlist_t **list, bool no_response)
{
    static uint8_t path_buffer[64];
    uint8_t buf[4];
    if (coap_uri_into_options(uri, server, list, 1, path_buffer, sizeof(path_buffer)) < 0)
        return false;
    coap_insert_optlist(list, coap_new_optlist(COAP_OPTION_CONTENT_FORMAT,
                        coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON),
                        buf));
    if (no_response) {
        // No-Response value 2 = "I don't want a 2.xx (success) answer" (RFC 7967)
        coap_insert_optlist(list, coap_new_optlist(COAP_OPTION_NORESPONSE,
                            coap_encode_var_safe(buf, sizeof(buf), 2), buf));
    }
    return true;
}

bool coap_client_start(void)
{
    static coap_uri_t uri;

    coap_startup();
    coap_set_log_level(COAP_LOG_ERR);
    ctx = coap_new_context(NULL);
    if (!ctx) {
        ESP_LOGE(TAG, "coap_new_context() failed");
        return false;
    }
    coap_register_response_handler(ctx, on_reply);

    if (coap_split_uri((const uint8_t *)COAP_SERVER_URI, strlen(COAP_SERVER_URI), &uri) < 0) {
        ESP_LOGE(TAG, "invalid COAP_SERVER_URI: %s", COAP_SERVER_URI);
        return false;
    }
    coap_addr_info_t *info = coap_resolve_address_info(&uri.host, uri.port, uri.port, uri.port,
                                                       uri.port, 0, 1 << uri.scheme,
                                                       COAP_RESOLVE_TYPE_REMOTE);
    if (!info) {
        ESP_LOGE(TAG, "could not resolve the server address");
        return false;
    }
    coap_address_t server;
    memcpy(&server, &info->addr, sizeof(server));
    coap_proto_t protocol = info->proto;
    coap_free_address_info(info);

    if (!build_options(&uri, &server, &options_con, false) ||
        !build_options(&uri, &server, &options_non, true)) {
        ESP_LOGE(TAG, "could not build the CoAP options");
        return false;
    }
    session = coap_new_client_session(ctx, NULL, &server, protocol);
    if (!session) {
        ESP_LOGE(TAG, "coap_new_client_session() failed");
        return false;
    }
    ESP_LOGI(TAG, "ready to send to %s", COAP_SERVER_URI);
    return true;
}

bool coap_client_send(const char *text, bool confirmable, int timeout_ms)
{
    coap_pdu_t *message = coap_new_pdu(confirmable ? COAP_MESSAGE_CON : COAP_MESSAGE_NON,
                                       COAP_REQUEST_CODE_POST, session);
    if (!message) return false;
    uint8_t token[8];
    size_t token_length;
    coap_session_new_token(session, &token_length, token);
    coap_add_token(message, token_length, token);
    coap_add_optlist_pdu(message, confirmable ? &options_con : &options_non);
    coap_add_data(message, strlen(text), (const uint8_t *)text);

    got_reply = false;
    if (coap_send(session, message) == COAP_INVALID_MID) return false;
    if (!confirmable) return true;           // NON + No-Response: nothing comes back

    // CON: wait for the server's answer (ACK with the 2.04 response piggybacked)
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (!got_reply) {
        int64_t left_ms = (deadline - esp_timer_get_time()) / 1000;
        if (left_ms <= 0) break;
        if (coap_io_process(ctx, (uint32_t)left_ms) < 0) break;
    }
    return got_reply;
}

void coap_client_poll(void)
{
    if (ctx) coap_io_process(ctx, COAP_IO_NO_WAIT);   // handle late packets, retransmissions
}
