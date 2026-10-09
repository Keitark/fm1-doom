#!/usr/bin/env python3
"""Capture the FM-1 Doom display and position through its read-only CDC commands.

Requires pyserial and Pillow. The game pauses briefly while the frame is read;
END is sent even on failure, and firmware independently expires the hold.
"""
import argparse
import json
import re
import time
from pathlib import Path


def fields(line):
    return {key: int(value) for key, value in re.findall(r"(\w+)=(-?\d+)", line)}


def request(port, command, prefix, deadline):
    if time.monotonic() >= deadline:
        raise TimeoutError(f"No {prefix} reply before capture deadline")
    port.write((command + "\n").encode("ascii"))
    pending = bytearray()
    while time.monotonic() < deadline:
        pending.extend(port.readline())
        if not pending.endswith(b"\n"):
            continue
        line = pending.decode("ascii", errors="replace").strip()
        pending.clear()
        if line.startswith(("ERR ", "ERROR ")):
            raise RuntimeError(line)
        if line.startswith(prefix):
            return line
    raise TimeoutError(f"No {prefix} reply before capture deadline")


def startup_handshake(port, deadline):
    # Opening CDC/DTR resets the receive generation. Give it time to settle,
    # then retry only the read-only identity command if its first reply is lost.
    time.sleep(min(1, max(0, deadline - time.monotonic())))
    for _ in range(3):
        if time.monotonic() >= deadline:
            break
        try:
            request(port, "HELLO", "FM1DIAG/1 DOOM-FM1/1 ",
                    min(deadline, time.monotonic() + 1))
            return
        except TimeoutError:
            pass
    raise TimeoutError("No Doom HELLO reply before startup deadline")


def capture(port):
    deadline = time.monotonic() + 12
    startup_handshake(port, deadline)
    game = fields(request(port, "DOOM GAME", "DOOM GAME ", deadline))
    try:
        request(port, "DOOM FRAME BEGIN", "OK DOOM FRAME ", deadline)
        while True:
            info = fields(request(port, "DOOM FRAMEINFO", "DOOM FRAMEINFO ", deadline))
            if info.get("ready") == 1:
                break
            if info.get("state") != 1:
                raise RuntimeError(f"Frame request was cancelled: {info}")
        if (info.get("width"), info.get("height"), info.get("bytes")) != (160, 100, 16512):
            raise ValueError("Unsupported frame format")
        data = bytearray()
        while len(data) < info["bytes"]:
            line = request(port, f"DOOM FRAME READ {len(data)}", "DOOM FRAME ", deadline)
            chunk = fields(line)
            match = re.search(r"\bdata=([0-9A-Fa-f]+)$", line)
            if not match:
                raise ValueError("Malformed pixel reply")
            payload = bytes.fromhex(match.group(1))
            if (chunk.get("id") != info["id"] or chunk.get("offset") != len(data)
                    or chunk.get("bytes") != len(payload)
                    or not 1 <= len(payload) <= min(96, info["bytes"] - len(data))):
                raise ValueError("Pixel sequence or length changed during capture")
            data.extend(payload)
        return bytes(data), {"game": game, "frame": info}
    finally:
        # Sending END requires no response; firmware also has a 15-second limit.
        port.write(b"DOOM FRAME END\n")


def display_pixels(data, coarse, menu):
    if len(data) != 16512:
        raise ValueError("Expected 16000 indexed pixels and 256 RGB565 colours")
    palette = []
    for index in range(256):
        pixel = int.from_bytes(data[16000 + index * 2:16002 + index * 2], "little")
        r, g, b = pixel >> 11, (pixel >> 5) & 63, pixel & 31
        palette.append((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    source = bytes(value for index in data[:16000] for value in palette[index])
    display = bytearray()
    for y in range(240):
        sy = y * 100 // 240 if menu else (y * 93 // 224 if y < 224
                                         else 93 + (y - 224) * 7 // 16)
        if coarse and not menu and y < 224:
            sy = (y // 8) * 93 // 28
        for x in range(240):
            sx = (x // 8) * 160 // 30 if coarse and not menu and y < 224 else x * 159 // 239
            display.extend(palette[data[sy * 160 + sx]])
    return source, bytes(display)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--output", type=Path, default=Path("build/usb-frame/screen.png"))
    args = parser.parse_args()
    import serial
    from PIL import Image
    with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=1) as port:
        data, metadata = capture(port)
    source, display = display_pixels(data, metadata["frame"]["coarse"], metadata["frame"]["menu"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    Image.frombytes("RGB", (240, 240), display).save(args.output)
    Image.frombytes("RGB", (160, 100), source).save(args.output.with_name(args.output.stem + "-source.png"))
    args.output.with_suffix(".bin").write_bytes(data)
    args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"screen": str(args.output.resolve()), **metadata}, indent=2))


if __name__ == "__main__":
    main()
