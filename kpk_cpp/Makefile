# Makefile — builds the C++ PKLauncher and every app in apps_src/
#
# Usage:
#   make          builds launcher + all apps
#   make clean    removes built binaries (keeps *.icon.json)
#
# Requires g++ with C++17 support (Armbian's default g++ is fine).

CXX      := g++
CXXFLAGS := -O2 -std=c++17 -Wall
LIBS     := -lm

APP_SOURCES := $(wildcard apps_src/*.cpp)
APP_BINARIES := $(patsubst apps_src/%.cpp,apps/%,$(APP_SOURCES))

.PHONY: all clean

all: launcher $(APP_BINARIES)

launcher: launcher.cpp pixelkit.hpp icon_io.hpp scroll_letters.hpp scroll_numbers.hpp scroll_symbols.hpp
	$(CXX) $(CXXFLAGS) -o $@ launcher.cpp $(LIBS)

apps/%: apps_src/%.cpp pixelkit.hpp scroll_letters.hpp scroll_numbers.hpp scroll_symbols.hpp | apps
	$(CXX) $(CXXFLAGS) -I. -o $@ $< $(LIBS)

apps:
	mkdir -p apps

clean:
	rm -f launcher $(APP_BINARIES)
