# PKLauncher for the Kickstarter Kano Pixel Kit (Linux)
# The launcher requires a venv with the pyserial, rpcclient, pixelkit,
# scroll_letters, scroll_numbers, and scroll_symbols installed

import math
import random
import time

import pixelkit as kit

# Connect to the kit's MCU
kit.connect()

# Clear the matrix
kit.clear()
kit.render()

# Define variables
GRID_W, GRID_H = kit.WIDTH, kit.HEIGHT
 
HEAD_COLOR   = (190, 255, 190)   # near-white leading pixel
TAIL_MIN     = 20                # dimmest green in a fading tail
TAIL_MAX     = 200                # brightest green just behind the head
TICK         = 0.07              # seconds per frame
 
SPEED_RANGE  = (0.5, 1.3)        # rows per frame, randomised per column
TRAIL_RANGE  = (3, 6)            # tail length, randomised per column

columns = [{} for _ in range(GRID_W)]

# Define functions for animation and drawing
# These functions make a falling rain effect similar to The Matrix and M5Launcher
def spawn_drop(col, fully_random=False):
    col['speed'] = random.uniform(*SPEED_RANGE)
    col['trail'] = random.randint(*TRAIL_RANGE)
    if fully_random:
        # Used at startup so columns are desynced from frame one.
        col['head'] = random.uniform(-GRID_H, GRID_H)
    else:
        col['head'] = -random.uniform(1, 6)

def reshuffle_drops():
    for col in columns:
        spawn_drop(col, fully_random=True)

def draw_matrix():
    kit.clear()
    for x, col in enumerate(columns):
        head_row = col['head']
        trail = col['trail']
        for dy in range(trail + 1):
            y = int(head_row) - dy
            if 0 <= y < GRID_H:
                if dy == 0:
                    kit.set_pixel(x, y, HEAD_COLOR)
                else:
                    frac = 1 - dy / (trail + 1)
                    g = int(TAIL_MIN + (TAIL_MAX - TAIL_MIN) * frac)
                    kit.set_pixel(x, y, (0, g, 0))

def animate_matrix():
    for col in columns:
        col['head'] += col['speed']
        if col['head'] - col['trail'] > GRID_H:
            spawn_drop(col)

t = 0
while True:
    kit.check_controls()
 
    draw_matrix()
    animate_matrix()
    kit.render()
 
    t += 1
    time.sleep(TICK)
