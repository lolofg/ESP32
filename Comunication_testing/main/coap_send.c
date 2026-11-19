#include <string.h>
#include "esp_log.h"
#include "coap3/coap.h"
#include "coap_send.h"
#include "secrets.h"

static const char *TAG = "coap";
static coap_context_t *ctx = NULL;          // libcoap's "workspace"
static coap_session_t *session = NULL;      // the connection to server
static coap_optlist_t *options = NULL;      // options added to every message (path, format)
static volatile bool got_reply = false;

// Called by libcoap when the server answers
static coap_response_t on_reply(coap_session_t *s, const coap_pdu_t *sent,
                                const coap_pdu_t *received, const coap_mid_t mid)
{
    coap_pdu_code_t code = coap_pdu_get_code(received);
    ESP_LOGI(TAG, "server answered %d.%02d", COAP_RESPONSE_CLASS(code), code & 0x1F);
    got_reply = true;
    return COAP_RESPONSE_OK;
}

bool coap_client_start(void)
{
    static coap_uri_t uri;
    static uint8_t path_buffer[64];

    coap_startup();
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


    coap_addr_info_t *info = coap_resolve_address_info(&uri.host, uri.port, uri.port, uri.port, uri.port,
                                                       0, 1 << uri.scheme, COAP_RESOLVE_TYPE_REMOTE);
    if (!info) {
        ESP_LOGE(TAG, "could not resolve the server address");
        return false;
    }
    coap_address_t server;
    memcpy(&server, &info->addr, sizeof(server));
    coap_proto_t protocol = info->proto;
    coap_free_address_info(info);


    coap_uri_into_options(&uri, &server, &options, 1, path_buffer, sizeof(path_buffer));
    uint8_t format[4];
    coap_insert_optlist(&options, coap_new_optlist(COAP_OPTION_CONTENT_FORMAT,
                        coap_encode_var_safe(format, sizeof(format), COAP_MEDIATYPE_APPLICATION_JSON),
                        format));
                  
    session = coap_new_client_session(ctx, NULL, &server, protocol);
    if (!session) {
        ESP_LOGE(TAG, "coap_new_client_session() failed");
        return false;
    }
    ESP_LOGI(TAG, "ready to send to %s", COAP_SERVER_URI);
    return true;
}

bool coap_client_send(const char *text, bool confirmable)
{
    coap_pdu_t *message = coap_new_pdu(confirmable ? COAP_MESSAGE_CON : COAP_MESSAGE_NON,
                                       COAP_REQUEST_CODE_POST, session);
    if (!message) {
        ESP_LOGE(TAG, "coap_new_pdu() failed");
        return false;
    }
    uint8_t token[8];
    size_t token_length;
    coap_session_new_token(session, &token_length, token);
    coap_add_token(message, token_length, token);
    coap_add_optlist_pdu(message, &options);
    coap_add_data(message, strlen(text), (const uint8_t *)text);

    got_reply = false;
    coap_send(session, message); 

    int waited_ms = 0;
    while (!got_reply && waited_ms < 2000) {
        int spent = coap_io_process(ctx, 100);
        if (spent < 0) {
            break;
        }
        waited_ms += spent > 0 ? spent : 1;
    }
    if (!got_reply) {
        ESP_LOGW(TAG, "no answer from the server");
    }
    return got_reply;
}