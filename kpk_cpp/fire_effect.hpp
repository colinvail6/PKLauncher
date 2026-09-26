#pragma once
/*
 * fire_effect.hpp - header-only fire simulation, mirrors FastFire.py
 * (C++ port of MatrixFireFast: https://github.com/toggledbits/MatrixFireFast)
 *
 * Usage:
 *   FireEffect fire;
 *   fire.configure(PK_W, PK_H, 15);   // width, height, fps, then any
 *                                     // of: colors, flare_rows,
 *                                     // max_flare, flare_chance,
 *                                     // flare_decay (see FireOptionalConfig)
 *   fire.reset();
 *   while (true) {
 *       fire.step();
 *       fire.draw(kit);
 *   }
 *
 * Every pixel has a heat value (0..ncolors-1). Heat is generated at the
 * base row (self-sustaining embers), decays by 1 as it convects upward
 * one row per frame, and random circular "flares" occasionally bloom
 * near the base for brighter flame tongues. A fixed palette maps heat
 * to color — this is a real heat diffusion simulation, not per-pixel
 * random color.
 *
 * Coordinate note: pix row 0 is the hottest base row; pixelkit's y=0 is
 * the top of the display, so pix row 0 is drawn at kit y=height-1
 * (bottom), and pix row height-1 (coolest) at kit y=0 (top).
 */

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
#include "pixelkit.hpp"

struct FireOptionalConfig {
    std::vector<RGB> colors;        // empty = use default palette
    int flare_rows   = -1;          // -1 = unset, use current value
    int max_flare    = -1;
    int flare_chance = -1;
    int flare_decay  = -1;
};

class FireEffect {
public:
    float framerate = 15.0f;

    void configure(int w, int h, float fps, const FireOptionalConfig &opt = {}) {
        width  = w;
        height = h;
        framerate = fps;
        if (!opt.colors.empty()) {
            colors  = opt.colors;
            ncolors = (int)colors.size();
        }
        if (opt.flare_rows   >= 0) flare_rows   = opt.flare_rows;
        if (opt.max_flare    >= 0) max_flare    = opt.max_flare;
        if (opt.flare_chance >= 0) flare_chance = opt.flare_chance;
        if (opt.flare_decay  >= 0) flare_decay  = opt.flare_decay;
    }

    void reset() {
        pix.assign(height, std::vector<uint8_t>(width, 0));
        flares.clear();
        for (int j = 0; j < width; j++) pix[0][j] = (uint8_t)(ncolors - 1);
    }

    void spark() { new_flare(true); }

    void step() {
        // Convect existing heat upward (toward higher row index) and decay it
        for (int i = height - 1; i > 0; i--) {
            for (int j = 0; j < width; j++) {
                pix[i][j] = pix[i - 1][j] > 0 ? pix[i - 1][j] - 1 : 0;
            }
        }

        // Re-heat the base row wherever it's still glowing (self-sustaining embers)
        int lo = std::max(0, ncolors - 6);
        int hi = std::max(lo, ncolors - 3);
        for (int j = 0; j < width; j++) {
            if (pix[0][j] > 0) pix[0][j] = (uint8_t)irand(lo, hi);
        }

        // Age out existing flares, dropping any that have burned out
        std::vector<Flare> remaining;
        for (auto &f : flares) {
            glow(f.x, f.y, f.z);
            if (f.z > 1) remaining.push_back({f.x, f.y, f.z - 1});
        }
        flares = remaining;

        new_flare(false);
    }

    void draw(PixelKit &kit) const {
        for (int i = 0; i < height; i++) {
            int ky = (height - 1) - i;   // pix row 0 (hottest) -> kit y=height-1 (bottom)
            for (int j = 0; j < width; j++) {
                kit.set_pixel(j, ky, colors[pix[i][j]]);
            }
        }
    }

private:
    struct Flare { int x, y, z; };

    int width  = 16;
    int height = 8;

    int flare_rows   = 2;
    int max_flare    = 4;
    int flare_chance = 50;
    int flare_decay  = 28;

    std::vector<RGB> colors = {
        {0x00, 0x00, 0x00}, {0x10, 0x00, 0x00}, {0x30, 0x00, 0x00},
        {0x60, 0x00, 0x00}, {0x80, 0x00, 0x00}, {0xA0, 0x00, 0x00},
        {0xC0, 0x20, 0x00}, {0xC0, 0x40, 0x00}, {0xC0, 0x60, 0x00},
        {0xC0, 0x80, 0x00}, {0x80, 0x70, 0x80},
    };
    int ncolors = (int)colors.size();

    std::vector<std::vector<uint8_t>> pix;
    std::vector<Flare> flares;

    std::mt19937 rng{std::random_device{}()};

    int irand(int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    }
    static int isqrt_(int n) { return (int)std::sqrt((double)n); }

    void glow(int x, int y, int z) {
        int b = z * 10 / flare_decay + 1;
        for (int i = y - b; i < y + b; i++) {
            for (int j = x - b; j < x + b; j++) {
                if (i >= 0 && i < height && j >= 0 && j < width) {
                    int d = (flare_decay * isqrt_((x - j) * (x - j) + (y - i) * (y - i)) + 5) / 10;
                    int n = (z > d) ? (z - d) : 0;
                    if (n > pix[i][j]) pix[i][j] = (uint8_t)n;
                }
            }
        }
    }

    void new_flare(bool force) {
        if ((int)flares.size() < max_flare && (force || irand(1, 100) <= flare_chance)) {
            int x = irand(0, width - 1);
            int y = irand(0, flare_rows - 1);
            int z = ncolors - 1;
            flares.push_back({x, y, z});
            glow(x, y, z);
        }
    }
};
