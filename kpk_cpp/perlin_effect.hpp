#pragma once
/*
 * perlin_effect.hpp - header-only 3D Perlin noise effect, ported from
 * the retail Kano Pixel Kit's CircuitPython original:
 * https://github.com/colinvail6/awesome-pixelkit/blob/main/code/circuitpython/fastperlin.py
 *
 * Classic Perlin noise (permutation table + fade-curve lookup + gradient
 * blending over the 8 corners of a unit cube), sampled on a moving Z
 * plane each frame so the pattern flows continuously rather than
 * sitting static.
 *
 * Differs from the retail version in two deliberate ways:
 *
 *   - scale is config-driven rather than read live off the dial. The
 *     retail kit's dial is a continuous 0-65535 ADC reading used for
 *     live zoom; on this kit the dial only reports 5 discrete
 *     positions and is already used for category selection, so a
 *     fixed config.json scale is the right fit here instead.
 *
 *   - color comes from a generic Palette (a list of RGB stops,
 *     linearly interpolated) instead of five hardcoded effect
 *     functions. The retail version's Rainbow/Grayscale/Fire/Water are
 *     all expressible as palettes — "rainbow" is special-cased to use
 *     true HSV hue cycling (see sample_color()) since a circular hue
 *     wheel doesn't interpolate cleanly as RGB stops. "Plasma" (the
 *     retail version's two-octave noise combo) is generalized into a
 *     separate `octaves` setting, so any palette can be combined with
 *     1 or 2 noise octaves rather than only "plasma" getting the
 *     second octave. A fully custom palette (any list of [r,g,b]
 *     stops) can also be supplied directly from config.json.
 *
 *   The retail fire/water color functions are also piecewise and have
 *   a couple of band-boundary discontinuities (brightness visibly
 *   jumps at noise=0.2 and noise=0.7). The built-in PALETTE_FIRE/
 *   PALETTE_WATER below reproduce their intent as smooth continuous
 *   gradients instead, rather than porting the discontinuity along
 *   with them.
 *
 * Usage:
 *   PerlinEffect perlin;
 *   perlin.configure(PK_W, PK_H, 20);   // width, height, fps, then any
 *                                        // of the fields in
 *                                        // PerlinOptionalConfig
 *   perlin.reset();
 *   while (true) {
 *       perlin.step();
 *       perlin.draw(kit);
 *   }
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "pixelkit.hpp"

// ---------------------------------------------------------------------------
// Palettes — a Palette is just a list of RGB stops, evenly spaced across
// [0,1] and linearly interpolated between neighbors.

using Palette = std::vector<RGB>;

static const Palette PALETTE_GRAYSCALE = {
    {0, 0, 0}, {255, 255, 255},
};

static const Palette PALETTE_FIRE = {
    {0, 0, 0}, {85, 0, 0}, {255, 0, 0}, {255, 255, 0}, {255, 255, 255},
};

static const Palette PALETTE_WATER = {
    {0, 0, 50}, {0, 0, 255}, {0, 255, 255}, {255, 255, 255},
};

static RGB sample_palette(const Palette &pal, float t) {
    if (pal.empty()) return {0, 0, 0};
    if (pal.size() == 1) return pal[0];
    t = std::max(0.0f, std::min(1.0f, t));
    float scaled = t * (pal.size() - 1);
    int   i0     = (int)scaled;
    int   i1     = std::min(i0 + 1, (int)pal.size() - 1);
    float frac   = scaled - i0;
    return {
        (uint8_t)(pal[i0].r + (pal[i1].r - pal[i0].r) * frac),
        (uint8_t)(pal[i0].g + (pal[i1].g - pal[i0].g) * frac),
        (uint8_t)(pal[i0].b + (pal[i1].b - pal[i0].b) * frac),
    };
}

// ---------------------------------------------------------------------------
// Config

struct PerlinOptionalConfig {
    float scale   = -1.0f;   // negative = unset, use current value
    float z_speed = -1000.0f;   // sentinel well outside any real z_speed
    int   octaves = -1;      // 1 or 2 (2 = retail's "plasma" layering)
    int   seed    = -1;      // -1 = unset -> random seed each reset()

    bool        has_palette_name   = false;
    std::string palette_name;      // "rainbow"/"grayscale"/"fire"/"water"
    bool        has_custom_palette = false;
    Palette     custom_palette;    // inline [r,g,b] stops, overrides palette_name
};

class PerlinEffect {
public:
    float framerate = 20.0f;

    void configure(int w, int h, float fps, const PerlinOptionalConfig &opt = {}) {
        width     = w;
        height    = h;
        framerate = fps;

        if (opt.scale >= 0.0f) scale = opt.scale;
        if (opt.z_speed > -999.0f) z_speed = opt.z_speed;
        if (opt.octaves == 1 || opt.octaves == 2) octaves = opt.octaves;
        if (opt.seed >= 0) { seed = opt.seed; have_fixed_seed = true; }

        if (opt.has_custom_palette && !opt.custom_palette.empty()) {
            palette_mode   = PaletteMode::Custom;
            custom_palette = opt.custom_palette;
        } else if (opt.has_palette_name) {
            palette_mode = name_to_mode(opt.palette_name);
        }
    }

    void reset() {
        rng.seed(have_fixed_seed ? (unsigned)seed : std::random_device{}());
        build_permutation();
        z_offset = 0.0f;
    }

    void step() {
        z_offset += z_speed;
    }

    void draw(PixelKit &kit) const {
        float nz  = z_offset;
        float nz2 = z_offset * 1.5f;
        for (int y = 0; y < height; y++) {
            float ny  = y * scale;
            float ny2 = y * scale * 2.0f;
            for (int x = 0; x < width; x++) {
                float nx = x * scale;
                float noise_val;
                if (octaves >= 2) {
                    float nx2 = x * scale * 2.0f;
                    float v1  = noise(nx, ny, nz);
                    float v2  = noise(nx2, ny2, nz2) * 0.5f;
                    noise_val = (v1 + v2 + 1.0f) / 2.5f;
                } else {
                    noise_val = (noise(nx, ny, nz) + 1.0f) * 0.5f;
                }
                kit.set_pixel(x, y, sample_color(noise_val));
            }
        }
    }

private:
    enum class PaletteMode { Rainbow, Grayscale, Fire, Water, Custom };

    int width  = 16;
    int height = 8;

    float scale   = 0.3f;
    float z_speed = 0.05f;
    int   octaves = 1;
    int   seed    = 0;
    bool  have_fixed_seed = false;

    PaletteMode palette_mode = PaletteMode::Rainbow;
    Palette     custom_palette;

    std::array<uint8_t, 512> perm{};
    std::array<float, 256>   fade_table{};
    std::mt19937 rng{std::random_device{}()};
    float z_offset = 0.0f;

    static PaletteMode name_to_mode(const std::string &name) {
        if (name == "grayscale") return PaletteMode::Grayscale;
        if (name == "fire")      return PaletteMode::Fire;
        if (name == "water")     return PaletteMode::Water;
        return PaletteMode::Rainbow;   // "rainbow" or unrecognized
    }

    void build_permutation() {
        std::array<uint8_t, 256> p;
        for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
        // Fisher-Yates, matching the original's manual shuffle
        // (CircuitPython has no random.shuffle)
        for (int i = 255; i > 0; i--) {
            int j = std::uniform_int_distribution<int>(0, i)(rng);
            std::swap(p[i], p[j]);
        }
        for (int i = 0; i < 256; i++) {
            perm[i]       = p[i];
            perm[i + 256] = p[i];   // doubled, so perm[a+1] never overflows
        }
        for (int i = 0; i < 256; i++) {
            fade_table[i] = fade_raw(i / 255.0f);
        }
    }

    static float fade_raw(float t) {
        return t * t * t * (t * (t * 6 - 15) + 10);
    }

    float fade(float t) const {
        int idx = (int)(t * 255);
        if (idx < 0) idx = 0;
        if (idx > 255) idx = 255;
        return fade_table[idx];
    }

    static float lerp(float t, float a, float b) {
        return a + t * (b - a);
    }

    // Simplified 3-bit gradient hash, matching the original exactly
    static float grad(int hash_val, float x, float y, float z) {
        int h = hash_val & 7;
        if (h < 4) return (h & 1) == 0 ? x + y : -x + y;
        return (h & 1) == 0 ? y + z : -y + z;
    }

    float noise(float x, float y, float z) const {
        // (int) truncates toward zero, matching Python's int() exactly —
        // deliberately NOT std::floor, which rounds differently for
        // negative inputs (possible if z_speed is ever configured
        // negative). This is a faithful port, not a reimplementation.
        int X = (int)x & 255;
        int Y = (int)y & 255;
        int Z = (int)z & 255;

        x -= (int)x;
        y -= (int)y;
        z -= (int)z;

        float u = fade(x);
        float v = fade(y);
        float w = fade(z);

        int a  = perm[X] + Y;
        int aa = perm[a] + Z;
        int ab = perm[a + 1] + Z;
        int b  = perm[X + 1] + Y;
        int ba = perm[b] + Z;
        int bb = perm[b + 1] + Z;

        return lerp(w,
            lerp(v,
                lerp(u, grad(perm[aa], x, y, z), grad(perm[ba], x - 1, y, z)),
                lerp(u, grad(perm[ab], x, y - 1, z), grad(perm[bb], x - 1, y - 1, z))),
            lerp(v,
                lerp(u, grad(perm[aa + 1], x, y, z - 1), grad(perm[ba + 1], x - 1, y, z - 1)),
                lerp(u, grad(perm[ab + 1], x, y - 1, z - 1), grad(perm[bb + 1], x - 1, y - 1, z - 1))));
    }

    RGB sample_color(float noise_val) const {
        switch (palette_mode) {
            case PaletteMode::Grayscale: return sample_palette(PALETTE_GRAYSCALE, noise_val);
            case PaletteMode::Fire:      return sample_palette(PALETTE_FIRE, noise_val);
            case PaletteMode::Water:     return sample_palette(PALETTE_WATER, noise_val);
            case PaletteMode::Custom:    return sample_palette(custom_palette, noise_val);
            case PaletteMode::Rainbow:
            default:
                return PixelKit::hsv(noise_val, 1.0f, 0.8f);   // h is 0-1, matches noise_val directly
        }
    }
};
