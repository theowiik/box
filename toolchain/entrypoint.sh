#!/usr/bin/env bash
# Load ESP-IDF (idf.py, its Python and compilers), then run the command.
. "$IDF_PATH/export.sh" >/dev/null 2>&1 || {
    echo "box-env: could not load ESP-IDF from $IDF_PATH" >&2
    exit 1
}
exec "$@"
