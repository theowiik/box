# app_config

Reads application configuration through the public `mosaico_shared_get()` API.
Vibe Mode owns provisioning; applications consume saved values in the
`sysmeta` NVS partition, `shared` namespace. This component depends on the public
Recovery adapter, ESP-IDF error types and Mbed TLS zeroization. It does not import
private Vibe Mode code, initialize Wi-Fi, write NVS, cache or log values.

Copied from the Mosaico Hello World template; the brightness reader is the box's addition.

`app_config_load_brightness()` reads Vibe Mode's `vibe_prefs/brightness`
preference read-only (20-100 %, falling back to 80 %).

`app_config_load_wifi()` reads `wifi_ssid` and `wifi_password` into a caller-owned
`app_config_wifi_t`. No initialization or teardown is required. Call from task
context with exclusive access to the output; concurrent calls with separate
outputs share no component state. Reads are synchronous and use the NVS API's
timing; there is no retry loop or application deadline. The pair uses two reads,
so callers must serialize any application-side shared-configuration writer.
Vibe Mode provisions the values while its firmware is running; the application
reads them after switching firmware.

On success, SSID is 1..32 bytes. Password is either an empty string for an open
network, 8..63 printable ASCII bytes, or a 64-digit hexadecimal PSK. Buffers include
the trailing NUL. Both keys must exist: a missing password is not an open network.
The loader does not truncate strings. It returns `ESP_ERR_NVS_NOT_FOUND` for a
missing namespace/key, `ESP_ERR_NVS_INVALID_LENGTH` for an oversized value,
`ESP_ERR_INVALID_ARG` for invalid credentials or NULL output, and propagates
other storage errors. Every failure clears a non-NULL output in full.
Successful reads describe saved configuration, not current network connectivity.

The device entry point demonstrates loading and checking the result after
Recovery starts. Missing or invalid configuration leaves Hello World's UI
available. It logs status only and clears its temporary snapshot with
`mbedtls_platform_zeroize()`. A future networking component can consume this
snapshot before it is cleared, copy the validated byte lengths into ESP-IDF's
32/64-byte station arrays, and handle connection/retry behavior separately.
Full-length station arrays do not need a trailing NUL; do not truncate them.
Use `WIFI_STORAGE_RAM` when applying shared credentials to avoid a second
persistent credential store.

For other shared strings, use the same public API directly in the consuming
device component. For example, configure a custom `api_key` in
Vibe Mode > Settings > Shared information, then:

```c
#include "mbedtls/platform_util.h"
#include "mosaico_shared.h"

static esp_err_t use_shared_api_key(void)
{
    char api_key[257] = {0};
    esp_err_t err = mosaico_shared_get("api_key", api_key, sizeof(api_key));
    if (err == ESP_OK) {
        /* Validate for your service, then consume the key here without logging it.
         * Finish any asynchronous use or transfer ownership before clearing. */
    }
    mbedtls_platform_zeroize(api_key, sizeof(api_key));
    return err; /* Caller handles missing/invalid configuration without erasing NVS. */
}
```

Declare `esp_mosaico_app_recovery` and `mbedtls` as private CMake dependencies for
that example. Shared keys contain 1..15 lowercase ASCII letters, digits or
underscores; general values contain up to 256 UTF-8 bytes plus NUL. Custom keys
have application-defined meaning; validate their contents and coordinate names
because shared information is common to all Ideas. Factory reset clears it.
Keep application-owned runtime state separate. Never hardcode real credentials,
log their values, erase `sysmeta` on an initialization error, or silently fall
back to a different credential store.

The PC backend builds only the portable UI controller and its host adapter.
It does not read device NVS. Future host integrations should inject explicit test
configuration at their platform boundary.
