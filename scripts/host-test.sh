#!/usr/bin/env sh
set -eu

build_dir=${HOST_TEST_BUILD_DIR:-build-host}
rm -rf "$build_dir"
cmake -S host_tests -B "$build_dir" -G Ninja
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
python3 -m py_compile tools/mock_companion.py host_tests/test_mock_companion.py
python3 host_tests/test_mock_companion.py
