#!/usr/bin/env python3
"""Sends key presses through a virtual input device, for testing on a box.

Run as root in the box's session seat. Each argument is one step:

    NAME            tap the key KEY_NAME (e.g. ENTER, BACK, DOWN, HOMEPAGE)
    hold:NAME:SECS  hold the key for SECS seconds
    sleep:SECS      wait
    NAME*N          tap N times

The device emits evdev codes after the point where fire-blaster would have
remapped a real remote, so what an app sees matches a remote press.
"""

import sys
import time

from evdev import UInput, ecodes

TAP_SECONDS = 0.06
GAP_SECONDS = 0.25


def code(name):
    key = "KEY_" + name.upper()
    if not hasattr(ecodes, key):
        sys.exit(f"unknown key {name}")
    return getattr(ecodes, key)


def main(steps):
    keys = {value for name, value in vars(ecodes).items() if name.startswith("KEY_") and isinstance(value, int)}
    keys = sorted(k for k in keys if 0 < k < 0x2ff)
    with UInput({ecodes.EV_KEY: keys}, name="ember-test-remote") as device:
        # Let libinput and the compositor pick the device up.
        time.sleep(1.0)
        for step in steps:
            if step.startswith("sleep:"):
                time.sleep(float(step.split(":", 1)[1]))
                continue
            hold = TAP_SECONDS
            count = 1
            if step.startswith("hold:"):
                _, name, seconds = step.split(":")
                hold = float(seconds)
            elif "*" in step:
                name, times = step.split("*")
                count = int(times)
            else:
                name = step
            for _ in range(count):
                device.write(ecodes.EV_KEY, code(name), 1)
                device.syn()
                time.sleep(hold)
                device.write(ecodes.EV_KEY, code(name), 0)
                device.syn()
                time.sleep(GAP_SECONDS)
        time.sleep(0.3)


if __name__ == "__main__":
    main(sys.argv[1:])
