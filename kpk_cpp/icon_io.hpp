#pragma once
/*
 * icon_io.hpp - loads *.icon.json files into an RGB[PK_H][PK_W] array.
 *
 * The icon format is always exactly: 8 rows of 16 [r, g, b] triples,
 * e.g. [[[0,0,0],[255,0,0],...16...], ...8 rows...]. Because the shape
 * is fixed and never contains strings, objects, or nesting beyond
 * this, a full JSON library is unnecessary — we just scan for every
 * integer in the file, in order, and pack them 3-at-a-time into pixels.
 * This keeps the launcher dependency-free for an offline embedded target.
 */

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include "pixelkit.hpp"

/* Fills icon[PK_H][PK_W] from path. Returns false on any read/parse
 * problem (missing file, wrong pixel count, etc.) — caller should fall
 * back to a placeholder icon in that case. */
static bool load_icon(const char *path, RGB icon[PK_H][PK_W]) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 1 << 20) { fclose(f); return false; }

    char *buf = (char *)malloc(size + 1);
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    buf[n] = '\0';

    int values[PK_H * PK_W * 3];
    int count = 0;
    char *p = buf;

    while (*p && count < PK_H * PK_W * 3) {
        /* Skip to the next digit or minus sign */
        while (*p && !(isdigit((unsigned char)*p) || *p == '-')) p++;
        if (!*p) break;
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        values[count++] = (int)v;
        p = end;
    }

    free(buf);

    if (count != PK_H * PK_W * 3) return false;

    int idx = 0;
    for (int y = 0; y < PK_H; y++) {
        for (int x = 0; x < PK_W; x++) {
            uint8_t r = (uint8_t)values[idx++];
            uint8_t g = (uint8_t)values[idx++];
            uint8_t b = (uint8_t)values[idx++];
            icon[y][x] = {r, g, b};
        }
    }
    return true;
}

/* Fallback icon when a *.icon.json is missing or fails to parse */
static void checkerboard_icon(RGB icon[PK_H][PK_W]) {
    for (int y = 0; y < PK_H; y++) {
        for (int x = 0; x < PK_W; x++) {
            bool lit = ((x + y) % 2) == 0;
            icon[y][x] = lit ? RGB{50, 50, 50} : RGB{0, 0, 0};
        }
    }
}
