#!/usr/bin/env python3
"""Exercise actual GTK DND on check-ui.sh's private Xvfb display."""
import ctypes as c
import json
import os
from pathlib import Path
import struct
import sys
import time
import zlib

assert os.environ.get("SELECT_FOR_FIGMA_PRIVATE_BUS") == "1"
request = Path(sys.argv[1])
for _ in range(600):
    if request.exists():
        break
    time.sleep(0.05)
else:
    raise SystemExit("No drag request from the diagnostic")
data = json.loads(request.read_text())
# Load the configured library first so its Nix dependencies are available by name.
xt = c.CDLL(os.environ["SELECT_FOR_FIGMA_XTEST_LIBRARY"])
x11 = c.CDLL("libX11.so.6")
x11.XOpenDisplay.restype = c.c_void_p
x11.XOpenDisplay.argtypes = [c.c_char_p]
x11.XDefaultRootWindow.restype = c.c_ulong
x11.XDefaultRootWindow.argtypes = [c.c_void_p]
x11.XTranslateCoordinates.argtypes = [c.c_void_p, c.c_ulong, c.c_ulong, c.c_int, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_ulong)]
x11.XFlush.argtypes = [c.c_void_p]
x11.XCloseDisplay.argtypes = [c.c_void_p]
xt.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
xt.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
display = x11.XOpenDisplay(os.environ["DISPLAY"].encode())
assert display
root = x11.XDefaultRootWindow(display)
rx, ry, child = c.c_int(), c.c_int(), c.c_ulong()
assert x11.XTranslateCoordinates(display, data["window"], root, 0, 0, c.byref(rx), c.byref(ry), c.byref(child))
start = (rx.value + data["start"][0], ry.value + data["start"][1])
end = (rx.value + data["end"][0], ry.value + data["end"][1])
if "end_root" in data:
    end = tuple(data["end_root"])
xt.XTestFakeMotionEvent(display, -1, *start, 0)
if data.get("click"):
    xt.XTestFakeButtonEvent(display, data["button"], 1, 0)
    x11.XFlush(display)
    time.sleep(0.1)
    xt.XTestFakeButtonEvent(display, data["button"], 0, 0)
    x11.XFlush(display)
else:
    xt.XTestFakeButtonEvent(display, 1, 1, 0)
    x11.XFlush(display)
    time.sleep(0.1)
    for i in range(1, 13):
        point = (round(start[0] + (end[0]-start[0])*i/12), round(start[1]+(end[1]-start[1])*i/12))
        xt.XTestFakeMotionEvent(display, -1, *point, 0)
        x11.XFlush(display)
        time.sleep(0.04)
if data.get("shortcuts"):
    x11.XStringToKeysym.argtypes = [c.c_char_p]
    x11.XStringToKeysym.restype = c.c_ulong
    x11.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
    x11.XKeysymToKeycode.restype = c.c_uint
    xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
    for shortcut in data["shortcuts"]:
        codes = [x11.XKeysymToKeycode(display, x11.XStringToKeysym(key.encode())) for key in shortcut]
        assert all(codes)
        for code in codes:
            xt.XTestFakeKeyEvent(display, code, 1, 0)
        for code in reversed(codes):
            xt.XTestFakeKeyEvent(display, code, 0, 0)
        x11.XFlush(display)
        time.sleep(0.15)
if data.get("keys"):
    # Exercise GTK key events and its input method, rather than emitting the
    # application's commit signal directly. This is restricted to the private bus.
    x11.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
    x11.XKeysymToKeycode.restype = c.c_uint
    xt.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
    for character in data["keys"]:
        code = x11.XKeysymToKeycode(display, 0xFF0D if character == "\n" else ord(character))
        assert code
        for pressed in (1, 0):
            xt.XTestFakeKeyEvent(display, code, pressed, 0)
            x11.XFlush(display)
            time.sleep(0.008)
time.sleep(data.get("dwell", 0.4))

class XImage(c.Structure):
    _fields_ = [("width", c.c_int), ("height", c.c_int), ("xoffset", c.c_int), ("format", c.c_int), ("data", c.c_void_p),
                ("byte_order", c.c_int), ("bitmap_unit", c.c_int), ("bitmap_bit_order", c.c_int), ("bitmap_pad", c.c_int),
                ("depth", c.c_int), ("bytes_per_line", c.c_int), ("bits_per_pixel", c.c_int), ("red_mask", c.c_ulong),
                ("green_mask", c.c_ulong), ("blue_mask", c.c_ulong), ("obdata", c.c_void_p), ("functions", c.c_void_p*6)]
x11.XGetImage.restype = c.POINTER(XImage)
x11.XGetImage.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_ulong, c.c_int]
x11.XDestroyImage.argtypes = [c.POINTER(XImage)]
capture = x11.XGetImage(display, root, 0, 0, 1200, 900, c.c_ulong(-1).value, 2)
assert capture and capture.contents.bits_per_pixel == 32
im = capture.contents
pixels = c.string_at(im.data, im.bytes_per_line * im.height)
rows = bytearray()
for y in range(im.height):
    rows.append(0)
    row = pixels[y*im.bytes_per_line:y*im.bytes_per_line+im.width*4]
    for x in range(0, len(row), 4):
        rows.extend((row[x+2], row[x+1], row[x]))
def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind+payload))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", im.width, im.height, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
request.with_suffix(".screen.png").write_bytes(png)
x11.XDestroyImage(capture)
if not data.get("click"):
    xt.XTestFakeButtonEvent(display, 1, 0, 0)
x11.XFlush(display)
x11.XCloseDisplay(display)
request.with_suffix(".done").write_text("done")
