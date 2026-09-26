/*
 * apps_src/snake.cpp — Snake (C++ port of apps/snake.py)
 *
 * Controls:
 *   Joystick up/down/left/right — steer (can't reverse into yourself)
 *   Button A                    — restart after game over
 *   Button Reset                — exit to launcher
 *
 * The walls loop (go off one edge, appear on the other). Every body
 * segment gets its own random color when it's created; only the head
 * is fixed. Speed increases slightly with every apple eaten. Colliding
 * with your own body ends the game, and the final score scrolls across
 * the grid on a loop until you press A to play again.
 *
 * Build:
 *   g++ -O2 -std=c++17 -o ../apps/snake snake.cpp -lm
 */

#include <cstdio>
#include <deque>
#include <random>
#include <unistd.h>
#include <vector>
#include "pixelkit.hpp"

// ---------------------------------------------------------------------------
// Config

static const float START_INTERVAL = 0.22f;
static const float MIN_INTERVAL   = 0.09f;
static const float SPEEDUP        = 0.008f;

static const RGB HEAD_COLOR  = {60, 255, 60};
static const RGB FOOD_COLOR  = {220, 30, 30};
static const RGB FLASH_COLOR = {200, 0, 0};
static const RGB SCORE_COLOR = {255, 200, 0};

static const RGB BODY_PALETTE[] = {
    {220, 30, 30}, {220, 120, 0}, {220, 220, 0}, {60, 200, 60},
    {0, 200, 160}, {0, 140, 220}, {90, 60, 220}, {200, 0, 200},
    {220, 0, 110}, {255, 255, 255},
};
static const int NUM_BODY_COLORS = sizeof(BODY_PALETTE) / sizeof(BODY_PALETTE[0]);

enum Dir { UP, DOWN, LEFT, RIGHT };

static int DX[] = {0, 0, -1, 1};
static int DY[] = {-1, 1, 0, 0};
static Dir OPPOSITE[] = {DOWN, UP, RIGHT, LEFT};

// ---------------------------------------------------------------------------
// State

static PixelKit kit;
static std::mt19937 rng(std::random_device{}());

struct Point { int x, y; };

static std::deque<Point> snake;          // front = tail, back = head
static std::deque<RGB>   snake_colors;   // parallel to snake
static Dir   direction      = RIGHT;
static Dir   next_direction = RIGHT;
static Point food;
static int   score      = 0;
static float interval   = START_INTERVAL;
static bool  game_over   = false;
static int   flash_left  = 0;

static RGB random_body_color() {
    return BODY_PALETTE[std::uniform_int_distribution<int>(0, NUM_BODY_COLORS - 1)(rng)];
}

static bool on_snake(Point p, bool include_tail) {
    size_t n = snake.size();
    for (size_t i = 0; i < n; i++) {
        // index 0 is the tail (oldest segment) — it moves away this tick
        // unless we're eating, so it's excluded from the check in that case
        if (!include_tail && i == 0) continue;
        if (snake[i].x == p.x && snake[i].y == p.y) return true;
    }
    return false;
}

static void spawn_food() {
    std::vector<Point> free_cells;
    for (int x = 0; x < PK_W; x++)
        for (int y = 0; y < PK_H; y++) {
            Point p{x, y};
            if (!on_snake(p, true)) free_cells.push_back(p);
        }
    if (free_cells.empty()) { food = {0, 0}; return; }
    food = free_cells[std::uniform_int_distribution<size_t>(0, free_cells.size() - 1)(rng)];
}

static void reset_game() {
    snake.clear();
    snake_colors.clear();
    int cx = PK_W / 2, cy = PK_H / 2;
    snake.push_back({cx - 2, cy});
    snake.push_back({cx - 1, cy});
    snake.push_back({cx,     cy});
    for (size_t i = 0; i < snake.size(); i++) snake_colors.push_back(random_body_color());

    direction      = RIGHT;
    next_direction = RIGHT;
    score          = 0;
    interval       = START_INTERVAL;
    game_over      = false;
    spawn_food();
}

// ---------------------------------------------------------------------------
// Controls

static void steer(Dir d) {
    if (game_over) return;
    if (OPPOSITE[d] == direction) return;   // ignore instant reversal
    next_direction = d;
}

static void on_a() {
    if (game_over) reset_game();
}

// ---------------------------------------------------------------------------
// Game step

static void die() {
    game_over  = true;
    flash_left = 6;
    kit.beep(120, 0.25f);
}

static void step() {
    direction = next_direction;
    Point head = snake.back();
    int nx = (head.x + DX[direction] + PK_W) % PK_W;   // walls loop
    int ny = (head.y + DY[direction] + PK_H) % PK_H;
    Point next{nx, ny};

    bool will_eat = (next.x == food.x && next.y == food.y);

    if (on_snake(next, will_eat)) {   // check against tail too if we're eating
        die();
        return;
    }

    snake.push_back(next);
    snake_colors.push_back(random_body_color());

    if (will_eat) {
        score++;
        interval = std::max(MIN_INTERVAL, interval - SPEEDUP);
        kit.beep(880, 0.05f);
        spawn_food();
    } else {
        snake.pop_front();
        snake_colors.pop_front();
    }
}

// ---------------------------------------------------------------------------
// Drawing

static void draw_game() {
    kit.clear();
    kit.set_pixel(food.x, food.y, FOOD_COLOR);
    size_t last = snake.size() - 1;
    for (size_t i = 0; i < snake.size(); i++) {
        RGB color = (i == last) ? HEAD_COLOR : snake_colors[i];
        kit.set_pixel(snake[i].x, snake[i].y, color);
    }
}

static void draw_flash() {
    kit.set_background(FLASH_COLOR);
}

static void scroll_game_over() {
    char buf[32];
    snprintf(buf, sizeof buf, "score %d", score);
    kit.scroll(buf, SCORE_COLOR, {0, 0, 0}, 60);
}

// ---------------------------------------------------------------------------
// Main

int main() {
    kit.on_joystick_up    = []() { steer(UP); };
    kit.on_joystick_down  = []() { steer(DOWN); };
    kit.on_joystick_left  = []() { steer(LEFT); };
    kit.on_joystick_right = []() { steer(RIGHT); };
    kit.on_button_a       = on_a;
    kit.on_joystick_click = on_a;
    kit.on_button_reset   = []() { kit.clear(); kit.render(); std::exit(130); };

    kit.connect();
    reset_game();

    while (true) {
        kit.check_controls();

        if (game_over) {
            if (flash_left > 0) {
                draw_flash();
                kit.render();
                flash_left--;
                usleep(80000);
            } else {
                scroll_game_over();
                usleep(300000);
            }
        } else {
            step();
            if (!game_over) {
                draw_game();
                kit.render();
            }
            usleep((useconds_t)(interval * 1'000'000));
        }
    }
}
