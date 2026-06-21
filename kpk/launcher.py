# PKLauncher for the Kickstarter Kano Pixel Kit (Linux)
# The launcher requires a venv with the pyserial, rpcclient, pixelkit,
# scroll_letters, scroll_numbers, and scroll_symbols installed

import glob
import json
import math
import os
import random
import sys
import time
 
import pixelkit as kit

# Connect to the kit's MCU
kit.connect()

print("[launcher] connected!")

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
 
pk_color = [255, 0, 0] # change this to be any coolor you want!
 
APPS_DIR    = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'apps')
SLIDE_STEPS = 10
SLIDE_DELAY = 0.016   # ~60 fps slide

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

# These functions draw a PK in the middle of the screen
def draw_pk(t):
    # Slow brightness pulse so the watermark feels alive, not static.
    pulse = 0.75 + 0.25 * math.sin(t * 0.12)
    color = tuple(int(c * pulse) for c in pk_color)
    kit.draw_letter(4, 1, 'p', color)
    kit.draw_letter(8, 1, 'k', color)

# Define functions for ease of use
def draw_home_screen(t):
    draw_matrix()
    animate_matrix()
    draw_pk(t)

# Define functions for icons
# Fallback icon when an app has no .icon.json next to it.
def checkerboard_icon():
    return [
        [[50, 50, 50] if (x + y) % 2 == 0 else [0, 0, 0]
         for x in range(GRID_W)]
        for y in range(GRID_H)
    ]

# Loads a JSON icon from a specified path
def load_icon(path):
    try:
        with open(path) as f:
            data = json.load(f)
        assert len(data) == GRID_H
        assert all(len(row) == GRID_W for row in data)
        return data
    except Exception:
        return checkerboard_icon() # Returns a checkerboard if the specified file is not found

# Draws the icon that is being loaded
def draw_icon(icon):
    for y in range(GRID_H):
        for x in range(GRID_W):
            kit.set_pixel(x, y, icon[y][x])

# Makes a smooth transition between each icon's slide
def slide_transition(from_icon, to_icon, direction):
    # direction =  1 -> current slides left, new enters from right (next)
    # direction = -1 -> current slides right, new enters from left (prev)
    for step in range(1, SLIDE_STEPS + 1):
        offset = (step * GRID_W) // SLIDE_STEPS
        for y in range(GRID_H):
            for x in range(GRID_W):
                vx = x + direction * offset
                if 0 <= vx < GRID_W:
                    color = from_icon[y][vx]
                else:
                    color = to_icon[y][vx - direction * GRID_W]
                kit.set_pixel(x, y, color)
        kit.render()
        time.sleep(SLIDE_DELAY)

# Looks for apps and their respective icons
# Example: snake.py, snake.icon.json
def discover_apps():
    paths = sorted(glob.glob(os.path.join(APPS_DIR, '*.py')))
    found = []
    for path in paths:
        icon_path = path.replace('.py', '.icon.json')
        found.append({
            'path': path,
            'name': os.path.basename(path).replace('.py', ''),
            'icon': load_icon(icon_path),
        })
    return found

apps     = discover_apps()
selected = 0
busy     = False   # guards against input while a slide is animating

# Goes to the next slide and makes sure that the launcher is not busy
def go_next(direction):
    global selected, busy
    if busy or not apps:
        return
    busy = True
    prev_icon = apps[selected]['icon']
    selected  = (selected + direction) % len(apps)
    slide_transition(prev_icon, apps[selected]['icon'], direction)
    busy = False

# Launches the app that has been selected
def launch_selected():
    if not apps:
        return
    kit.clear()
    kit.render()
    os.execvp(sys.executable, [sys.executable, apps[selected]['path']])

# There are two screens, idle and browsing
state = 'idle'

# Wrap a control so the first press after the matrix screen just wakes
# the launcher into browse mode, without also performing the action.
# Every press after that runs the action normally. 
def wake_or(action):
    def handler():
        global state
        if state == 'idle':
            state = 'browse'
            draw_icon(apps[selected]['icon'] if apps else checkerboard_icon())
            kit.render()
        else:
            action()
    return handler

# Puts the launcher into idle mode
def go_idle():
    global state
    state = 'idle'
    reshuffle_drops()   # fresh rain pattern each time we return home

# Handles reset functions accordingly
def handle_reset():
    if state == 'idle':
        kit.interrupt()   # Reset on the home screen quits the launcher
    else:
        go_idle()         # Reset while browsing just backs out to home
 
kit.on_joystick_right = wake_or(lambda: go_next(1))
kit.on_joystick_left  = wake_or(lambda: go_next(-1))
kit.on_button_a       = wake_or(launch_selected)
kit.on_joystick_click = wake_or(launch_selected)
kit.on_button_reset   = handle_reset

print("[launcher] started")

t = 0
while True:
    kit.check_controls()
 
    if state == 'idle':
        draw_home_screen(t)
        kit.render()
    # 'browse' state doesn't redraw every frame — the icon is static until
    # go_next() or wake_or() repaints it, which already calls kit.render().
 
    t += 1
    time.sleep(TICK)
