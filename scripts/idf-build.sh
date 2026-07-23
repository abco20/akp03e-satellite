#!/usr/bin/env sh
set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
"$script_dir/idf.sh" set-target esp32s3
"$script_dir/idf.sh" build
