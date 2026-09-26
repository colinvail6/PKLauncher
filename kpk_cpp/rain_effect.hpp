#pragma once
/*
 * rain_effect.hpp - header-only rain effect, mirrors FastRain.py
 *
 * Deliberately different mechanics from a Matrix-style falling-code
 * effect: drops fall under gravity (accelerating) rather than scrolling
 * at a constant speed, spawn intermittently per column rather than every
 * column streaming continuously, use a short head+trail rather than a
 * long fading tail, and splash briefly on impact.
 *
 * Usage:
 *   RainEffect rain;
 *   rain.configure(PK_W, PK_H, 30);   // width, height, fps, then any
 *                                     // of the fields in RainOptionalConfig
 *   rain.reset();
 *   while (true) {
 *       rain.step();
 *       rain.draw(kit);
 *   }
 */

#include <random>
#include <vector>
#include "pixelkit.hpp"

struct RainOptionalConfig {
    float gravity       = -1.0f;   // negative = unset, use current value
    float speed_min     = -1.0f;
    float speed_max     = -1.0f;
    int   cooldown_min  = -1;
    int   cooldown_max  = -1;
    int   splash_frames = -1;
    float spawn_chance  = -1.0f;
    bool  has_head_color   = false; RGB head_color;
    bool  has_trail_color  = false; RGB trail_color;
    bool  has_splash_color = false; RGB splash_color;
};

class RainEffect {
public:
    float framerate = 30.0f;

    void configure(int w, int h, float fps, const RainOptionalConfig &opt = {}) {
        width  = w;
        height = h;
        framerate = fps;
        if (opt.gravity       >= 0.0f) gravity       = opt.gravity;
        if (opt.speed_min     >= 0.0f) speed_min     = opt.speed_min;
        if (opt.speed_max     >= 0.0f) speed_max     = opt.speed_max;
        if (opt.cooldown_min  >= 0)    cooldown_min  = opt.cooldown_min;
        if (opt.cooldown_max  >= 0)    cooldown_max  = opt.cooldown_max;
        if (opt.splash_frames >= 0)    splash_frames = opt.splash_frames;
        if (opt.spawn_chance  >= 0.0f) spawn_chance  = opt.spawn_chance;
        if (opt.has_head_color)   head_color   = opt.head_color;
        if (opt.has_trail_color)  trail_color  = opt.trail_color;
        if (opt.has_splash_color) splash_color = opt.splash_color;
    }

    void reset() {
        active.assign(width, false);
        y.assign(width, 0.0f);
        speed.assign(width, 0.0f);
        cooldown.assign(width, 0);
        splashes.clear();
    }

    void step() {
        for (int x = 0; x < width; x++) {
            if (!active[x]) {
                if (cooldown[x] > 0) {
                    cooldown[x]--;
                } else if (unit_rand() < spawn_chance) {
                    active[x] = true;
                    y[x]      = 0.0f;
                    speed[x]  = frand(speed_min, speed_max);
                }
                continue;
            }

            y[x]     += speed[x];
            speed[x] += gravity;

            if (y[x] >= height - 1) {
                y[x]        = (float)(height - 1);
                active[x]   = false;
                cooldown[x] = irand(cooldown_min, cooldown_max);
                splashes.push_back({x, splash_frames});
            }
        }

        std::vector<Splash> remaining;
        for (auto &s : splashes) {
            if (s.frames_left - 1 >= 0) remaining.push_back({s.x, s.frames_left - 1});
        }
        splashes = remaining;
    }

    void draw(PixelKit &kit) const {
        kit.clear();

        for (int x = 0; x < width; x++) {
            if (active[x]) {
                int head_y = (int)y[x];
                kit.set_pixel(x, head_y, head_color);
                if (head_y > 0) kit.set_pixel(x, head_y - 1, trail_color);
            }
        }

        for (auto &s : splashes) {
            int row = height - 1;
            kit.set_pixel(s.x, row, splash_color);
            if (s.x > 0)        kit.set_pixel(s.x - 1, row, splash_color);
            if (s.x < width - 1) kit.set_pixel(s.x + 1, row, splash_color);
        }
    }

private:
    struct Splash { int x, frames_left; };

    int width  = 16;
    int height = 8;

    float gravity       = 0.12f;
    float speed_min     = 0.3f;
    float speed_max     = 0.6f;
    int   cooldown_min  = 5;
    int   cooldown_max  = 25;
    int   splash_frames = 2;
    float spawn_chance  = 0.05f;

    RGB head_color   = {130, 190, 255};
    RGB trail_color  = {40,  70,  140};
    RGB splash_color = {80,  130, 200};

    std::vector<bool>  active;
    std::vector<float> y;
    std::vector<float> speed;
    std::vector<int>   cooldown;
    std::vector<Splash> splashes;

    std::mt19937 rng{std::random_device{}()};

    float frand(float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    }
    int irand(int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    }
    float unit_rand() {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
    }
};
