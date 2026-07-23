#!/usr/bin/env python3
"""Minimal Companion Satellite API server for first hardware bring-up.

It registers the ESP32 client, paints six distinguishable 60x60 keys, sends
brightness, and prints all button/encoder events. It uses only the Python
standard library.
"""

from __future__ import annotations

import argparse
import base64
import socket
import threading
import time
from typing import BinaryIO

WIDTH = 60
HEIGHT = 60
COLORS = [
    (255, 0, 0),
    (0, 255, 0),
    (0, 0, 255),
    (255, 255, 0),
    (0, 255, 255),
    (255, 0, 255),
]


def make_bitmap(index: int) -> str:
    color = COLORS[index]
    pixels = bytearray(WIDTH * HEIGHT * 3)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            offset = (y * WIDTH + x) * 3
            # A white L marker makes rotation obvious on the physical key.
            marker = (x < 5 and y < 35) or (y >= 30 and y < 35 and x < 25)
            if marker:
                pixels[offset : offset + 3] = b"\xff\xff\xff"
            else:
                pixels[offset + 0] = color[0]
                pixels[offset + 1] = color[1]
                pixels[offset + 2] = color[2]
    return base64.b64encode(pixels).decode("ascii")


def send_line(stream: BinaryIO, line: str) -> None:
    if " BITMAP=" in line:
        prefix, bitmap = line.split(" BITMAP=", 1)
        print(f"> {prefix} BITMAP=<base64:{len(bitmap)} chars>")
    else:
        print(f"> {line}")
    data = memoryview((line + "\n").encode("utf-8"))
    while data:
        written = stream.write(data)
        if written is None or written <= 0:
            raise ConnectionError("socket closed during write")
        data = data[written:]
    stream.flush()


def ping_loop(stream: BinaryIO, stop: threading.Event) -> None:
    counter = 0
    while not stop.wait(2.0):
        counter += 1
        try:
            send_line(stream, f"PING mock-{counter}")
        except OSError:
            return


def serve_client(connection: socket.socket, peer: tuple[str, int], brightness: int) -> None:
    print(f"client connected: {peer[0]}:{peer[1]}")
    stop = threading.Event()
    with connection, connection.makefile("rwb", buffering=0) as stream:
        send_line(stream, "BEGIN CompanionVersion=mock ApiVersion=1.12.0")
        send_line(stream, "CAPS SUBSCRIPTIONS=1 NONSQUARE=1 BITMAP_FORMATS=rgb")

        ping_thread = threading.Thread(target=ping_loop, args=(stream, stop), daemon=True)
        ping_thread.start()
        registered = False
        try:
            while raw_line := stream.readline():
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
                print(f"< {line}")
                command = line.split(" ", 1)[0]
                if command == "ADD-DEVICE":
                    send_line(stream, "ADD-DEVICE OK")
                    send_line(stream, f"BRIGHTNESS DEVICEID=akp03e-esp32 VALUE={brightness}")
                    for key in range(6):
                        send_line(
                            stream,
                            "KEY-STATE DEVICEID=akp03e-esp32 "
                            f'CONTROLID="key/{key}" TYPE=BUTTON BITMAP={make_bitmap(key)}',
                        )
                    registered = True
                elif command == "PING":
                    payload = line[4:].lstrip()
                    send_line(stream, f"PONG {payload}")
                elif command in {"KEY-PRESS", "KEY-ROTATE"}:
                    if not registered:
                        print("warning: input arrived before ADD-DEVICE OK")
                elif command in {"PONG"}:
                    pass
                elif command == "QUIT":
                    return
                else:
                    send_line(stream, f'ERROR MESSAGE="Unknown command: {command}"')
        finally:
            stop.set()
            ping_thread.join(timeout=1.0)
            print("client disconnected")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=16622)
    parser.add_argument("--brightness", type=int, choices=range(0, 101), default=70,
                        metavar="0..100")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.bind, args.port))
        server.listen(1)
        print(f"mock Companion listening on {args.bind}:{args.port}")
        while True:
            connection, peer = server.accept()
            try:
                serve_client(connection, peer, args.brightness)
            except (ConnectionError, OSError) as error:
                print(f"connection error: {error}")


if __name__ == "__main__":
    main()
