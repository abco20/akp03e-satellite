#!/usr/bin/env sh
set -eu

if command -v idf.py >/dev/null 2>&1; then
    exec idf.py "$@"
fi

idf_path=${IDF_PATH:-${ESP_IDF_PATH:-}}
if [ -n "$idf_path" ] && [ -f "$idf_path/export.sh" ]; then
    # ESP-IDF's export script adds idf.py and its versioned toolchain to PATH.
    # shellcheck disable=SC1090
    . "$idf_path/export.sh" >/dev/null
    exec idf.py "$@"
fi

cat >&2 <<'EOF'
ESP-IDF was not found.

Install ESP-IDF v5.5.4, then either source its export.sh or set IDF_PATH:
  export IDF_PATH=/path/to/esp-idf

See: https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/
EOF
exit 127
