#!/usr/bin/env python3
from __future__ import annotations

import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def main() -> None:
    port = reserve_port()
    process = subprocess.Popen(
        [sys.executable, "-u", str(ROOT / "tools/mock_companion.py"), "--bind", "127.0.0.1", "--port", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        deadline = time.monotonic() + 3.0
        connection: socket.socket | None = None
        while time.monotonic() < deadline:
            try:
                connection = socket.create_connection(("127.0.0.1", port), timeout=1.0)
                break
            except OSError:
                time.sleep(0.05)
        if connection is None:
            raise AssertionError("mock server did not start")

        connection.settimeout(5.0)
        with connection, connection.makefile("rwb", buffering=0) as stream:
            assert stream.readline().decode().startswith("BEGIN ")
            assert stream.readline().decode().startswith("CAPS ")
            stream.write(b'ADD-DEVICE DEVICEID=x PRODUCT_NAME="x"\n')
            lines = [stream.readline().decode().strip() for _ in range(8)]
            assert lines[0] == "ADD-DEVICE OK"
            assert lines[1].startswith("BRIGHTNESS ")
            assert sum(line.startswith("KEY-STATE ") for line in lines) == 6
            stream.write(b'KEY-PRESS DEVICEID=x CONTROLID="key/0" PRESSED=1\n')
            stream.write(b"QUIT\n")
    finally:
        process.terminate()
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.kill()

    print("mock Companion integration test passed")


if __name__ == "__main__":
    main()
