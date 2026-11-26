// Wi-Fi station and mDNS, using the credentials saved in Vibe Mode.
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOX_NET_UNCONFIGURED, /**< No Wi-Fi saved in Vibe Mode; nothing to join. */
    BOX_NET_CONNECTING,   /**< Joining, or rejoining after a drop. */
    BOX_NET_CONNECTED,    /**< Has an address: `ip` is set. */
} box_net_status_t;

/** Called from the event loop task on every change; keep it short. */
typedef void (*box_net_status_cb_t)(box_net_status_t status, const char *ip, void *ctx);

/** Start joining the saved network. Retries forever with backoff. */
esp_err_t box_net_start(box_net_status_cb_t on_status, void *ctx);

#ifdef __cplusplus
}
#endif
