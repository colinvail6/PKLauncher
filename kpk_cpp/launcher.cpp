/*
 * launcher.cpp — PKLauncher, full C++ port
 *
 * Behavior:
 *   - Boots into an idle "matrix rain + pk" screen
 *   - Any button/joystick press wakes it into category select, showing
 *     the icon for whatever category the dial currently points at
 *   - Turning the dial slides between category icons (effects, games,
 *     utils, tools, misc — one per physical dial position)
 *   - Any button/joystick press confirms the dial-selected category and
 *     enters its app browser
 *   - Joystick left/right browses installed apps within that category
 *     (icon preview + slide)
 *   - Button A or joystick click launches the selected app
 *   - Button Reset backs out one level: browse -> category select ->
 *     idle -> quit
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
#include "config_parser.hpp"
#include "fire_effect.hpp"
#include "rain_effect.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Config — mirrors launcher.py's constants exactly

static const RGB  HEAD_COLOR   = {190, 255, 190};
static const int  TAIL_MIN     = 20;
static const int  TAIL_MAX     = 200;
static float TICK              = 0.07f;   // seconds per idle-screen frame;
                                           // overridden by config.json

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

// --- Home screen configuration ---------------------------------------------
// config.json (next to this binary) picks which effect runs on the idle
// screen — matrix (default), fire, or rain — and tunes it. A missing
// file, missing fields, or a bad file all fall back to these defaults,
// which reproduce the original matrix-only behavior exactly. See
// CONFIG.md for the full field reference.

enum class IdleEffect { Matrix, Fire, Rain };
static IdleEffect g_idle_effect = IdleEffect::Matrix;
static bool       g_show_pk     = true;

static FireEffect fire_effect;
static RainEffect rain_effect;

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

// Reads config.json (if present) and sets up whichever idle effect it
// requests. Never throws or aborts — any problem (missing file, bad
// JSON, unknown effect name) just falls back to the matrix default,
// exactly matching the original hardcoded behavior.
static void load_home_screen_config(const std::string &config_path) {
    JsonValue empty_cfg;
    JsonValue loaded_cfg;
    bool have_config = false;

    auto result = parse_json_file(config_path);
    if (result.ok) {
        loaded_cfg   = result.value;
        have_config  = true;
    } else {
        // Only warn if the file actually exists but failed to parse —
        // a simply-absent file is the common default case, not an error.
        FILE *probe = fopen(config_path.c_str(), "rb");
        if (probe) {
            fclose(probe);
            fprintf(stderr, "[launcher] config.json error (%s), using defaults\n",
                    result.error.c_str());
        }
    }
    const JsonValue &cfg = have_config ? loaded_cfg : empty_cfg;

    std::string idle_effect_str = cfg["idle_effect"].as_string("matrix");
    g_show_pk = cfg["show_pk"].as_bool(true);
    auto pk = cfg["pk_color"].as_rgb({255, 0, 0});
    pk_color = {(uint8_t)pk.r, (uint8_t)pk.g, (uint8_t)pk.b};

    if (idle_effect_str == "fire") {
        g_idle_effect = IdleEffect::Fire;
        const JsonValue &fc = cfg["fire"];
        float fps = fc["fps"].as_float(fire_effect.framerate);

        FireOptionalConfig opt;
        if (fc["colors"].type == JsonType::Array) {
            for (auto &entry : fc["colors"].arrValue) {
                auto rgb = entry.as_rgb({0, 0, 0});
                opt.colors.push_back({(uint8_t)rgb.r, (uint8_t)rgb.g, (uint8_t)rgb.b});
            }
        }
        opt.flare_rows   = fc["flare_rows"].as_int(-1);
        opt.max_flare    = fc["max_flare"].as_int(-1);
        opt.flare_chance = fc["flare_chance"].as_int(-1);
        opt.flare_decay  = fc["flare_decay"].as_int(-1);

        fire_effect.configure(PK_W, PK_H, fps, opt);
        fire_effect.reset();
        TICK = 1.0f / fps;

    } else if (idle_effect_str == "rain") {
        g_idle_effect = IdleEffect::Rain;
        const JsonValue &rc = cfg["rain"];
        float fps = rc["fps"].as_float(rain_effect.framerate);

        RainOptionalConfig opt;
        opt.gravity       = rc["gravity"].as_float(-1.0f);
        opt.speed_min     = rc["speed_min"].as_float(-1.0f);
        opt.speed_max     = rc["speed_max"].as_float(-1.0f);
        opt.cooldown_min  = rc["cooldown_min"].as_int(-1);
        opt.cooldown_max  = rc["cooldown_max"].as_int(-1);
        opt.splash_frames = rc["splash_frames"].as_int(-1);
        opt.spawn_chance  = rc["spawn_chance"].as_float(-1.0f);

        if (!rc["head_color"].is_null()) {
            auto rgb = rc["head_color"].as_rgb({130, 190, 255});
            opt.has_head_color = true;
            opt.head_color = {(uint8_t)rgb.r, (uint8_t)rgb.g, (uint8_t)rgb.b};
        }
        if (!rc["trail_color"].is_null()) {
            auto rgb = rc["trail_color"].as_rgb({40, 70, 140});
            opt.has_trail_color = true;
            opt.trail_color = {(uint8_t)rgb.r, (uint8_t)rgb.g, (uint8_t)rgb.b};
        }
        if (!rc["splash_color"].is_null()) {
            auto rgb = rc["splash_color"].as_rgb({80, 130, 200});
            opt.has_splash_color = true;
            opt.splash_color = {(uint8_t)rgb.r, (uint8_t)rgb.g, (uint8_t)rgb.b};
        }

        rain_effect.configure(PK_W, PK_H, fps, opt);
        rain_effect.reset();
        TICK = 1.0f / fps;

    } else {
        if (idle_effect_str != "matrix") {
            fprintf(stderr, "[launcher] unknown idle_effect '%s' in config.json, "
                            "using matrix\n", idle_effect_str.c_str());
        }
        g_idle_effect = IdleEffect::Matrix;
        float fps = cfg["matrix"]["fps"].as_float(1.0f / 0.07f);
        TICK = 1.0f / fps;
    }
}

static void draw_home_screen(PixelKit &kit, int t) {
    switch (g_idle_effect) {
        case IdleEffect::Fire:
            fire_effect.step();
            fire_effect.draw(kit);
            break;
        case IdleEffect::Rain:
            rain_effect.step();
            rain_effect.draw(kit);
            break;
        default:
            draw_matrix(kit);
            animate_matrix();
            break;
    }
    if (g_show_pk) draw_pk(kit, t);
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

// ---------------------------------------------------------------------------
// Categories — one per physical dial position. apps/<name>/ holds that
// category's app executables + their icon.json files, exactly like the
// old flat apps/ did. apps/category_icons/<name>.icon.json is a separate,
// single representative icon shown while picking a category (distinct
// from any individual app's icon, so the two never collide by name).

static const char *CATEGORY_NAMES[] = {"effects", "games", "utils", "tools", "misc"};
static const int   NUM_CATEGORIES   = sizeof(CATEGORY_NAMES) / sizeof(CATEGORY_NAMES[0]);

static RGB category_icons[NUM_CATEGORIES][PK_H][PK_W];
static std::string apps_root_dir;   // set once in main(), e.g. ".../apps"

static void load_category_icons() {
    for (int i = 0; i < NUM_CATEGORIES; i++) {
        std::string path = apps_root_dir + "/category_icons/" +
                            CATEGORY_NAMES[i] + ".icon.json";
        if (!load_icon(path.c_str(), category_icons[i])) {
            fprintf(stderr, "[launcher] could not load category icon '%s' "
                            "(falling back to checkerboard)\n", path.c_str());
            checkerboard_icon(category_icons[i]);
        }
    }
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
// State machine: idle (matrix screen) <-> category select (dial) <-> browse

enum class State { Idle, CategorySelect, Browse };

static State state          = State::Idle;
static int   category_index = 0;   // tracks the dial continuously, in every
                                    // state — only drawn on screen while
                                    // CategorySelect
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

static void enter_category_select() {
    state = State::CategorySelect;
    draw_icon(kit, category_icons[category_index]);
    kit.render();
}

/* Scans apps/<category>/ fresh every time, rather than once at startup —
 * so an app dropped in while the launcher is already running (e.g. via
 * a future upload feature) shows up next time you browse that category,
 * no restart needed. */
static void enter_browse() {
    state    = State::Browse;
    apps     = discover_apps(apps_root_dir + "/" + CATEGORY_NAMES[category_index]);
    selected = 0;

    if (apps.empty()) {
        // Distinct from a missing/broken icon file (which falls back to
        // a checkerboard) — this is a legitimately empty category, so
        // say so explicitly rather than showing the same ambiguous
        // placeholder for two different situations.
        kit.scroll("empty", {80, 80, 80}, {0, 0, 0}, 60);
        kit.clear();
        kit.render();
        return;
    }

    draw_icon(kit, reinterpret_cast<RGB(*)[PK_W]>(current_icon()));
    kit.render();
}

/* Wraps a control so the first press at Idle advances to category select
 * (without performing the action), the first press at CategorySelect
 * confirms the dial's current category and advances to browse (also
 * without performing the action), and only once in Browse does the
 * control run its actual action. */
static std::function<void()> advance_or(std::function<void()> action) {
    return [action]() {
        if (state == State::Idle) {
            enter_category_select();
        } else if (state == State::CategorySelect) {
            enter_browse();
        } else {
            action();
        }
    };
}

/* The dial is tracked continuously regardless of state — turning it at
 * Idle or Browse just silently updates which category is "pointed at"
 * for next time; only CategorySelect actually animates the change,
 * since that's the only state where a category icon is on screen. */
static void on_dial_changed(int new_index) {
    if (new_index < 0 || new_index >= NUM_CATEGORIES) {
        return;   // out-of-range value — ignore entirely, never adopt it
    }
    if (new_index == category_index) {
        return;   // no change — nothing to redraw
    }

    if (state == State::CategorySelect && !busy) {
        busy = true;
        int delta = new_index - category_index;
        int direction = (delta == 1 || delta == -(NUM_CATEGORIES - 1)) ? 1
                       : (delta == -1 || delta == (NUM_CATEGORIES - 1)) ? -1
                       : (delta > 0 ? 1 : -1);   // fallback for a multi-step jump
        RGB prev[PK_H][PK_W];
        std::copy(&category_icons[category_index][0][0],
                  &category_icons[category_index][0][0] + PK_H * PK_W, &prev[0][0]);
        category_index = new_index;
        slide_transition(kit, prev, category_icons[category_index], direction);
        busy = false;
    } else {
        category_index = new_index;
    }
}

static void go_idle() {
    state = State::Idle;
    switch (g_idle_effect) {
        case IdleEffect::Fire: fire_effect.reset(); break;
        case IdleEffect::Rain: rain_effect.reset(); break;
        default:               reshuffle_drops();   break;
    }
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
    switch (state) {
        case State::Idle:            interrupt();              break;
        case State::CategorySelect:  go_idle();                break;
        case State::Browse:          enter_category_select();  break;
    }
}

// ---------------------------------------------------------------------------
// Main

int main(int argc, char **argv) {
    kit.on_joystick_right = advance_or([]() { go_next(1); });
    kit.on_joystick_left  = advance_or([]() { go_next(-1); });
    kit.on_joystick_up    = advance_or([]() {});   // no action at Browse yet
    kit.on_joystick_down  = advance_or([]() {});
    kit.on_button_a       = advance_or(launch_selected);
    kit.on_joystick_click = advance_or(launch_selected);
    kit.on_button_reset   = handle_reset;
    kit.on_dial           = on_dial_changed;

    if (!kit.connect()) {
        fprintf(stderr, "failed to connect to Pixel Kit\n");
        return 1;
    }

    kit.clear();
    kit.render();

    // config.json and apps/ both live next to this binary, wherever it
    // was invoked from
    std::string exe_dir = fs::canonical(fs::path(argv[0])).parent_path().string();
    load_home_screen_config(exe_dir + "/config.json");
    apps_root_dir = exe_dir + "/apps";
    load_category_icons();

    for (auto &col : columns) spawn_drop(col, true);

    int t = 0;
    while (true) {
        kit.check_controls();

        if (state == State::Idle) {
            draw_home_screen(kit, t);
            kit.render();
        }
        // CategorySelect/Browse don't redraw every frame — the icon is
        // static until go_next()/advance_or()/on_dial_changed() repaints
        // it, which already renders.

        t++;
        usleep((useconds_t)(TICK * 1'000'000));
    }
}
