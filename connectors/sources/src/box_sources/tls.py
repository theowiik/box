"""TLS for fetching feeds, even where Python cannot find the system's CAs.

Some Pythons are built to look for CA certificates where this system keeps
none — uv's standalone Python on NixOS looks for /etc/ssl/cert.pem — and then
every HTTPS feed fails verification. Rather than turn verification off, fall
back to the places distributions do keep the bundle.
"""

import os
import ssl
from functools import lru_cache

#: Where distributions keep the CA bundle, most common first.
BUNDLES = (
    "/etc/ssl/certs/ca-certificates.crt",  # Debian, Ubuntu, Arch, NixOS
    "/etc/pki/tls/certs/ca-bundle.crt",  # Fedora, RHEL
    "/etc/ssl/cert.pem",  # macOS, Alpine
    "/etc/ssl/ca-bundle.pem",  # openSUSE
)


@lru_cache(maxsize=1)
def context() -> ssl.SSLContext:
    """A verifying context with CA certificates loaded from somewhere real."""
    ctx = ssl.create_default_context()
    if ctx.cert_store_stats()["x509_ca"] or os.getenv("SSL_CERT_FILE"):
        return ctx
    for path in BUNDLES:
        if os.path.isfile(path):
            ctx.load_verify_locations(cafile=path)
            break
    return ctx
