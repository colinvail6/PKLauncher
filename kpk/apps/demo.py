#!/usr/bin/env python3
"""
apps/demo.py — pixelkit feature demo

A small sandbox for testing the launcher and showing off pixelkit's
basics: drawing, scrolling text, joystick input, and the buzzer. Good
as a placeholder app while building out the launcher, or as a reference
for the minimum shape an app needs.

Controls:
  Joystick      — move the cursor pixel around
  Button A      — cycle the cursor's color
  Button B      — beep
  Button Reset  — exit to launcher
"""

import time

import pixelkit as kit

COLORS = [
    (255, 0, 0), (255, 140, 0), (255, 255, 0), (0, 220, 60),
    (0, 180, 255), (120, 60, 255), (255, 0, 200), (255, 255, 255),
]

cursor_x, cursor_y = kit.WIDTH // 2, kit.HEIGHT // 2
color_index = 0


def _move(dx, dy):
    global cursor_x, cursor_y
    cursor_x = max(0, min(kit.WIDTH - 1, cursor_x + dx))
    cursor_y = max(0, min(kit.HEIGHT - 1, cursor_y + dy))


def _cycle_color():
    global color_index
    color_index = (color_index + 1) % len(COLORS)


def _beep():
    kit.beep(660, 0.08)


kit.on_joystick_up    = lambda: _move(0, -1)
kit.on_joystick_down  = lambda: _move(0, 1)
kit.on_joystick_left  = lambda: _move(-1, 0)
kit.on_joystick_right = lambda: _move(1, 0)
kit.on_button_a       = _cycle_color
kit.on_button_b       = _beep
kit.on_button_reset   = kit.interrupt

kit.connect()
kit.scroll('demo', color=(0, 220, 255), interval=0.05)

while True:
    kit.check_controls()
    kit.clear()
    kit.set_pixel(cursor_x, cursor_y, COLORS[color_index])
    kit.render()
    time.sleep(0.04)
