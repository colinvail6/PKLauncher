# FastFire.py - a module based off of MatrixFireFast to create fire effects for the launcher
import pixelkit as kit
import time, random

# Defaults
width = 16
height = 8

def configure(w, h, framerate, colors): # example: fire.configure(16, 8, [list of colors])
  global width
  global height
  width = w
  height = h
  ncolors = (len(colors)/len(colors[0])
