# Makefile — builds the C++ PKLauncher and every app in apps/
#
# pixelkit.hpp and the scroll_*.hpp fonts live in the PKLib submodule
# (kpk_cpp/PKLib), not in this repo. First-time setup after cloning:
#
#   git submodule update --init --recursive
#
# To pull in newer PKLib changes later:
#
#   git submodule update --remote kpk_cpp/PKLib
#
# Usage:
#   make          builds launcher + all apps
#   make clean    removes built binaries (keeps *.cpp and *.icon.json)
#
# Requires g++ with C++17 support (Armbian's default g++ is fine).

CXX      := g++
CXXFLAGS := -O2 -std=c++17 -Wall
LIBS     := -lm

PKLIB_DIR     := PKLib/kpk/cpp
PKLIB_HEADERS := $(PKLIB_DIR)/pixelkit.hpp \
                 $(PKLIB_DIR)/scroll_letters.hpp \
                 $(PKLIB_DIR)/scroll_numbers.hpp \
                 $(PKLIB_DIR)/scroll_symbols.hpp

CXXFLAGS += -I$(PKLIB_DIR)

# App sources and their compiled binaries both live in apps/ —
# e.g. apps/snake.cpp builds to apps/snake, sitting right next to
# apps/snake.icon.json.
APP_SOURCES  := $(wildcard apps/*.cpp)
APP_BINARIES := $(APP_SOURCES:.cpp=)

.PHONY: all clean check-submodule

all: check-submodule launcher $(APP_BINARIES)

# Fails fast with a clear message instead of a wall of "No such file"
# errors if the submodule was never checked out.
check-submodule:
	@test -f $(PKLIB_DIR)/pixelkit.hpp || { \
		echo "error: PKLib submodule not found at $(PKLIB_DIR)"; \
		echo "  run: git submodule update --init --recursive"; \
		exit 1; \
	}

launcher: launcher.cpp icon_io.hpp $(PKLIB_HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ launcher.cpp $(LIBS)

apps/%: apps/%.cpp $(PKLIB_HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f launcher $(APP_BINARIES)