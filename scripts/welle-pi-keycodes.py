#!/usr/bin/env python3
"""
Print the key codes a remote control sends, for the "map" of the
evdev welle-pi input plugin (docs/development/welle-pi-input.md).

Usage: sudo python3 scripts/welle-pi-keycodes.py [--list] [/dev/input/eventN ...]

Without device arguments every readable /dev/input/event* that has keys is
listened to. Press the buttons of the remote, Ctrl-C to stop; the codes are
printed again at the end as lines that can be copied into welle-pi.json.

The script does not grab the device, so it can run next to a keyboard session
and stops working the moment welle-pi has grabbed the device itself.
"""

import argparse
import fcntl
import glob
import os
import re
import select
import struct
import sys

EV_KEY = 1
EV_MAX = 0x1F
KEY_NAMES_HEADER = "/usr/include/linux/input-event-codes.h"

# struct input_event is a timeval followed by type, code, value; "@" lets
# struct use the widths of this platform (16 bytes on 32 bit, 24 on 64 bit).
EVENT = struct.Struct("@llHHi")

# Used when the kernel headers are not installed.
FALLBACK_NAMES = {
    1: "KEY_ESC",
    103: "KEY_UP",
    108: "KEY_DOWN",
    105: "KEY_LEFT",
    106: "KEY_RIGHT",
    28: "KEY_ENTER",
    96: "KEY_KPENTER",
    352: "KEY_OK",
    353: "KEY_SELECT",
    102: "KEY_HOME",
    172: "KEY_HOMEPAGE",
    127: "KEY_COMPOSE",
    139: "KEY_MENU",
    158: "KEY_BACK",
    174: "KEY_EXIT",
    116: "KEY_POWER",
    356: "KEY_POWER2",
    142: "KEY_SLEEP",
    143: "KEY_WAKEUP",
    189: "KEY_F19",
    114: "KEY_VOLUMEDOWN",
    115: "KEY_VOLUMEUP",
    113: "KEY_MUTE",
    164: "KEY_PLAYPAUSE",
    272: "BTN_MOUSE",
    273: "BTN_RIGHT",
}


def _ioc(direction, number, size):
    return (direction << 30) | (size << 16) | (ord("E") << 8) | number


def _open(path):
    return os.open(path, os.O_RDONLY | os.O_NONBLOCK)


def device_name(fd):
    buf = bytearray(256)
    try:
        fcntl.ioctl(fd, _ioc(2, 0x06, len(buf)), buf, True)
    except OSError:
        return ""
    return buf.split(b"\0")[0].decode("utf-8", "replace")


def has_keys(fd):
    buf = bytearray((EV_MAX // 8) + 1)
    try:
        fcntl.ioctl(fd, _ioc(2, 0x20, len(buf)), buf, True)
    except OSError:
        return False
    return bool(buf[EV_KEY // 8] & (1 << (EV_KEY % 8)))


def key_names():
    names = {}
    try:
        with open(KEY_NAMES_HEADER, encoding="utf-8", errors="replace") as f:
            for line in f:
                match = re.match(r"#define\s+((?:KEY|BTN)_\w+)\s+(\w+)", line)
                if not match:
                    continue
                try:
                    code = int(match.group(2), 0)
                except ValueError:      # an alias, e.g. KEY_MIN_INTERESTING
                    continue
                names.setdefault(code, match.group(1))
    except OSError:
        pass
    return names or FALLBACK_NAMES


def main():
    parser = argparse.ArgumentParser(
            description="Print the key codes a remote control sends.")
    parser.add_argument("devices", nargs="*", metavar="/dev/input/eventN",
            help="devices to read (default: every readable event device)")
    parser.add_argument("--list", action="store_true",
            help="only list the devices and exit")
    args = parser.parse_args()

    names = key_names()
    devices = []
    for path in args.devices or sorted(glob.glob("/dev/input/event*")):
        try:
            fd = _open(path)
        except OSError as error:
            print(f"{path}: {error.strerror}", file=sys.stderr)
            continue
        name = device_name(fd)
        if not has_keys(fd):
            print(f'{path}: "{name}", no keys, skipped')
            os.close(fd)
            continue
        print(f'{path}: "{name}"')
        devices.append([path, name, fd])

    if not devices:
        print("\nNo readable keyboard found. Run this as root, or add yourself\n"
              "to the input group: sudo usermod -aG input $USER (then log in\n"
              "again). While welle-pi is running it grabs the remote, so stop\n"
              "the service first: sudo systemctl stop welle-pi", file=sys.stderr)
        return 1

    if args.list:
        for _, _, fd in devices:
            os.close(fd)
        return 0

    print("\nPress the buttons of the remote, Ctrl-C to stop.\n")
    seen = {}
    try:
        while True:
            ready, _, _ = select.select([fd for _, _, fd in devices], [], [])
            for device in list(devices):
                path, name, fd = device
                if fd not in ready:
                    continue
                try:
                    data = os.read(fd, EVENT.size * 64)
                except OSError as error:
                    print(f'{path}: "{name}" went away ({error.strerror})')
                    os.close(fd)
                    devices.remove(device)
                    continue
                for offset in range(0, len(data) - EVENT.size + 1, EVENT.size):
                    _, _, event_type, code, value = EVENT.unpack_from(data, offset)
                    if event_type != EV_KEY or value != 1:
                        continue
                    print(f"{os.path.basename(path):12s} code {code:4d}  "
                          f"{names.get(code, 'unknown')}")
                    seen[code] = names.get(code, "unknown")
                sys.stdout.flush()
            if not devices:
                break
    except KeyboardInterrupt:
        pass

    print("\n--- pressed, for the \"map\" of welle-pi.json ---")
    for code in sorted(seen):
        print(f'    "{code}": "<action>",   # {seen[code]}')
    print("\nActions: up, down, left, right, enter, menu, quit, none")
    return 0


if __name__ == "__main__":
    sys.exit(main())
