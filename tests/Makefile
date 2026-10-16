# Host tests for the ESPN parser and the saved Wi-Fi network list. Needs g++
# and the ArduinoJson single header next to this file (ArduinoJson.h, version
# 7.4.3 to match ESPHome).
CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -O1 -Wall -Wextra -I../components/gameday -I.
# Link the GCC runtime in on Windows. Git Bash puts its own older
# libstdc++-6.dll ahead of the compiler's on PATH, and the mismatched
# runtime faults on the first ifstream.
ifeq ($(OS),Windows_NT)
LDFLAGS += -static
endif

all: build/test_parse build/test_networks
	./build/test_parse
	./build/test_networks

SRCS = test_parse.cpp golden_football.cpp test_set.cpp ../components/gameday/espn_parse.cpp ../components/gameday/sport_baseball.cpp ../components/gameday/sport_soccer.cpp ../components/gameday/sport_basketball.cpp ../components/gameday/sport_hockey.cpp

build/test_parse: build/miniz.o $(SRCS) $(wildcard ../components/gameday/*.h) miniz.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) build/miniz.o -o $@ $(LDFLAGS)

# miniz v1.15 (public domain), the version in the ESP32-S3 ROM that the
# firmware inflates ESPN's gzip replies with.
build/miniz.o: miniz.c
	@mkdir -p build
	$(CC) -O1 -c miniz.c -o $@

build/test_networks: test_networks.cpp ../components/saved_networks/network_list.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I../components/saved_networks test_networks.cpp -o $@ $(LDFLAGS)

# Not part of `all`: replays a folder of recorded single-event documents
# (`.../scoreboard/<event id>`, named ev_<id>_<UTC time>.json) through the
# parse, text and splash code and reports failures and odd splashes.
#   make replay && ./build/replay <folder> <sport: 1 baseball, 2 basketball, 3 hockey, 4 soccer> [name prefix]
replay: build/replay

build/replay: replay.cpp $(filter-out test_parse.cpp golden_football.cpp test_set.cpp,$(SRCS)) $(wildcard ../components/gameday/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) replay.cpp $(filter-out test_parse.cpp golden_football.cpp test_set.cpp,$(SRCS)) -o $@ $(LDFLAGS)

clean:
	rm -rf build
.PHONY: all clean replay
