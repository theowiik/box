// The HTTP API connectors talk to.
//
//   POST   /api/events             submit an event (the connector API)
//   DELETE /api/events             drop queued events
//   GET    /api/display            what the panel shows, and its size
//   POST   /api/display/clear      drop the queue and the idle rotation
//   POST   /api/display/brightness {"brightness": 0..1}
//
// Writes require "Authorization: Bearer <token>" when CONFIG_BOX_TOKEN is set.
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start serving. Call after box_net_start(), which brings up TCP/IP; it
 *  does not need a Wi-Fi connection yet. */
esp_err_t box_http_start(void);

#ifdef __cplusplus
}
#endif
