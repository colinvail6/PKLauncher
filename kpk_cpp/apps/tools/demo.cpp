/*
 * apps_src/demo.cpp — pixelkit feature demo (C++ port of apps/demo.py)
 *
 * A small sandbox for testing the launcher and showing off pixelkit's
 * basics: drawing, scrolling text, joystick input, and the buzzer.
 *
 * Controls:
 *   Joystick      — move the cursor pixel around
 *   Button A      — cycle the cursor's color
 *   Button B      — beep
 *   Button Reset  — exit to launcher
 *
 * Build:
 *   g++ -O2 -std=c++17 -o ../apps/demo demo.cpp -lm
 */

#include <unistd.h>
#include "pixelkit.hpp"

static const RGB COLORS[] = {
    {255, 0,   0},   {255, 140, 0},   {255, 255, 0}, {0,   220, 60},
    {0,   180, 255}, {120, 60,  255}, {255, 0,   200}, {255, 255, 255},
};
static const int NUM_COLORS = sizeof(COLORS) / sizeof(COLORS[0]);

static PixelKit kit;
static int cursor_x = PK_W / 2;
static int cursor_y = PK_H / 2;
static int color_index = 0;

static void move(int dx, int dy) {
    cursor_x = std::max(0, std::min(PK_W - 1, cursor_x + dx));
    cursor_y = std::max(0, std::min(PK_H - 1, cursor_y + dy));
}

static void cycle_color() {
    color_index = (color_index + 1) % NUM_COLORS;
}

static void beep() {
    kit.beep(660, 0.08f);
}

int main() {
    kit.on_joystick_up    = []() { move(0, -1); };
    kit.on_joystick_down  = []() { move(0, 1); };
    kit.on_joystick_left  = []() { move(-1, 0); };
    kit.on_joystick_right = []() { move(1, 0); };
    kit.on_button_a       = cycle_color;
    kit.on_button_b       = beep;
    kit.on_button_reset   = []() { kit.clear(); kit.render(); std::exit(130); };

    kit.connect();
    kit.scroll("demo", {0, 220, 255}, {0, 0, 0}, 50);

    while (true) {
        kit.check_controls();
        kit.clear();
        kit.set_pixel(cursor_x, cursor_y, COLORS[color_index]);
        kit.render();
        usleep(40000);
    }
}
