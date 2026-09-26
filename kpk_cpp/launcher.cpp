/*
 * launcher.cpp — PKLauncher, full C++ port
 *
 * Mirrors launcher.py's behavior exactly:
 *   - Boots into an idle "matrix rain + pk" screen
 *   - Any button/joystick press wakes it into the app browser
 *   - Joystick left/right browses installed apps (icon preview + slide)
 *   - Button A or joystick click launches the selected app
 *   - Button Reset backs out one level: browse -> idle, idle -> quit
 *
 * Apps here are native executables (not scripts) — see apps_src/ for
 * examples. Each app binary sits in apps/ next to a <name>.icon.json.
 * Launching uses execv(), same process-replacement approach as the
 * Python version's os.execvp() — no extra process, no memory overhead.
 *
 * Build:
 *   g++ -O2 -std=c++17 -o launcher launcher.cpp -lm
 *
 * Run via run_launcher.sh (unchanged from the Python version — it just
 * checks the exit code, so it works identically whether launcher.py or
 * this binary is what it's restarting). Reset on the idle screen exits
 * with code 130, matching Python's KeyboardInterrupt exit code, so the
 * existing wrapper script needs no changes at all.
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

#include "pixelkit.hpp"
#include "icon_io.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Config — mirrors launcher.py's constants exactly

static const RGB  HEAD_COLOR   = {190, 255, 190};
static const int  TAIL_MIN     = 20;
static const int  TAIL_MAX     = 200;
static const float TICK        = 0.07f;   // seconds per idle-screen frame

static const float SPEED_MIN = 0.5f, SPEED_MAX = 1.3f;
static const int   TRAIL_MIN = 3,    TRAIL_MAX = 6;

static const int   SLIDE_STEPS = 10;
static const int   SLIDE_DELAY_US = 16000;   // ~60 fps slide

// ---------------------------------------------------------------------------
// RNG helpers

static std::mt19937 rng(std::random_device{}());

static float frand(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}
static int irand(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

// ---------------------------------------------------------------------------
// Idle screen — matrix rain with the "pk" watermark

struct Column {
    float head;
    float speed;
    int   trail;
};

static Column columns[PK_W];
static RGB    pk_color = {255, 0, 0};   // change this to any color you want!

static void spawn_drop(Column &col, bool fully_random) {
    col.speed = frand(SPEED_MIN, SPEED_MAX);
    col.trail = irand(TRAIL_MIN, TRAIL_MAX);
    if (fully_random) {
        col.head = frand(-(float)PK_H, (float)PK_H);
    } else {
        col.head = -frand(1.0f, 6.0f);
    }
}

static void reshuffle_drops() {
    for (auto &col : columns) spawn_drop(col, true);
}

static void draw_matrix(PixelKit &kit) {
    kit.clear();
    for (int x = 0; x < PK_W; x++) {
        Column &col = columns[x];
        int head_row = (int)col.head;
        for (int dy = 0; dy <= col.trail; dy++) {
            int y = head_row - dy;
            if (y < 0 || y >= PK_H) continue;
            if (dy == 0) {
                kit.set_pixel(x, y, HEAD_COLOR);
            } else {
                float frac = 1.0f - (float)dy / (col.trail + 1);
                int   g    = (int)(TAIL_MIN + (TAIL_MAX - TAIL_MIN) * frac);
                kit.set_pixel(x, y, {0, (uint8_t)g, 0});
            }
        }
    }
}

static void animate_matrix() {
    for (auto &col : columns) {
        col.head += col.speed;
        if (col.head - col.trail > PK_H) spawn_drop(col, false);
    }
}

static void draw_pk(PixelKit &kit, int t) {
    float pulse = 0.75f + 0.25f * sinf(t * 0.12f);
    RGB color = {
        (uint8_t)(pk_color.r * pulse),
        (uint8_t)(pk_color.g * pulse),
        (uint8_t)(pk_color.b * pulse),
    };
    kit.draw_letter(4, 1, 'p', color);
    kit.draw_letter(8, 1, 'k', color);
}

static void draw_home_screen(PixelKit &kit, int t) {
    draw_matrix(kit);
    animate_matrix();
    draw_pk(kit, t);
}

// ---------------------------------------------------------------------------
// App browser — discovers executables in apps/, shows icon.json previews

struct App {
    std::string exec_path;
    std::string name;
    RGB         icon[PK_H][PK_W];
};

static std::vector<App> discover_apps(const std::string &apps_dir) {
    std::vector<App> apps;
    if (!fs::exists(apps_dir)) return apps;

    std::vector<fs::path> exec_paths;
    for (auto &entry : fs::directory_iterator(apps_dir)) {
        if (!entry.is_regular_file()) continue;

        std::string fname = entry.path().filename().string();
        if (fname.size() > 10 &&
            fname.compare(fname.size() - 10, 10, ".icon.json") == 0) {
            continue;   // skip icon files themselves
        }

        auto perms = entry.status().permissions();
        bool executable = (perms & fs::perms::owner_exec) != fs::perms::none;
        if (!executable) continue;

        exec_paths.push_back(entry.path());
    }
    std::sort(exec_paths.begin(), exec_paths.end());

    for (auto &path : exec_paths) {
        App app;
        app.exec_path = fs::absolute(path).string();
        app.name      = path.filename().string();

        std::string icon_path = apps_dir + "/" + app.name + ".icon.json";
        if (!load_icon(icon_path.c_str(), app.icon)) {
            checkerboard_icon(app.icon);
        }
        apps.push_back(app);
    }
    return apps;
}

static void draw_icon(PixelKit &kit, RGB icon[PK_H][PK_W]) {
    for (int y = 0; y < PK_H; y++)
        for (int x = 0; x < PK_W; x++)
            kit.set_pixel(x, y, icon[y][x]);
}

static void slide_transition(PixelKit &kit, RGB from_icon[PK_H][PK_W],
                              RGB to_icon[PK_H][PK_W], int direction) {
    for (int step = 1; step <= SLIDE_STEPS; step++) {
        int offset = (step * PK_W) / SLIDE_STEPS;
        for (int y = 0; y < PK_H; y++) {
            for (int x = 0; x < PK_W; x++) {
                int vx = x + direction * offset;
                RGB color;
                if (vx >= 0 && vx < PK_W) {
                    color = from_icon[y][vx];
                } else {
                    color = to_icon[y][vx - direction * PK_W];
                }
                kit.set_pixel(x, y, color);
            }
        }
        kit.render();
        usleep(SLIDE_DELAY_US);
    }
}

// ---------------------------------------------------------------------------
// State machine: idle (matrix screen) <-> browse (app menu)

enum class State { Idle, Browse };

static State            state = State::Idle;
static std::vector<App> apps;
static size_t           selected = 0;
static bool             busy     = false;
static PixelKit         kit;

static RGB *current_icon() {
    static RGB fallback[PK_H][PK_W];
    if (apps.empty()) {
        checkerboard_icon(fallback);
        return &fallback[0][0];
    }
    return &apps[selected].icon[0][0];
}

static void go_next(int direction) {
    if (busy || apps.empty()) return;
    busy = true;

    RGB prev[PK_H][PK_W];
    std::copy(&apps[selected].icon[0][0], &apps[selected].icon[0][0] + PK_H * PK_W,
              &prev[0][0]);

    selected = (selected + apps.size() + direction) % apps.size();
    slide_transition(kit, prev, apps[selected].icon, direction);
    busy = false;
}

static void launch_selected() {
    if (apps.empty()) return;

    kit.clear();
    kit.render();
    if (kit.fd >= 0) { close(kit.fd); kit.fd = -1; }

    const std::string &path = apps[selected].exec_path;
    char *argv2[] = {const_cast<char *>(path.c_str()), nullptr};
    execv(path.c_str(), argv2);

    // execv only returns on failure
    perror("execv");
    _exit(1);
}

/* Wraps a control so the first press after the matrix screen just wakes
 * the launcher into browse mode, without also performing the action.
 * Every press after that runs the action normally. */
static std::function<void()> wake_or(std::function<void()> action) {
    return [action]() {
        if (state == State::Idle) {
            state = State::Browse;
            draw_icon(kit, reinterpret_cast<RGB(*)[PK_W]>(current_icon()));
            kit.render();
        } else {
            action();
        }
    };
}

static void go_idle() {
    state = State::Idle;
    reshuffle_drops();   // fresh rain pattern each time we return home
}

/* Mirrors kit.interrupt() from pixelkit.py: clear the display and exit
 * with code 130 (same as Python's KeyboardInterrupt exit code), so the
 * existing run_launcher.sh wrapper works identically without changes. */
static void interrupt() {
    kit.clear();
    kit.render();
    std::exit(130);
}

static void handle_reset() {
    if (state == State::Idle) {
        interrupt();   // Reset on the home screen quits the launcher
    } else {
        go_idle();      // Reset while browsing just backs out to home
    }
}

// ---------------------------------------------------------------------------
// Main

int main(int argc, char **argv) {
    kit.on_joystick_right = wake_or([]() { go_next(1); });
    kit.on_joystick_left  = wake_or([]() { go_next(-1); });
    kit.on_button_a       = wake_or(launch_selected);
    kit.on_joystick_click = wake_or(launch_selected);
    kit.on_button_reset   = handle_reset;

    if (!kit.connect()) {
        fprintf(stderr, "failed to connect to Pixel Kit\n");
        return 1;
    }

    kit.clear();
    kit.render();

    // apps/ lives next to this binary, wherever it was invoked from
    std::string exe_dir = fs::canonical(fs::path(argv[0])).parent_path().string();
    apps = discover_apps(exe_dir + "/apps");

    for (auto &col : columns) spawn_drop(col, true);

    int t = 0;
    while (true) {
        kit.check_controls();

        if (state == State::Idle) {
            draw_home_screen(kit, t);
            kit.render();
        }
        // Browse state doesn't redraw every frame — the icon is static
        // until go_next()/wake_or() repaints it, which already renders.

        t++;
        usleep((useconds_t)(TICK * 1'000'000));
    }
}
