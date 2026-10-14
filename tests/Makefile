# Host tests for the ESPN parser. Needs g++ and the ArduinoJson single header
# next to this file (ArduinoJson.h, version 7.4.3 to match ESPHome).
CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -O1 -Wall -Wextra -I../components/gameday -I.

all: build/test_parse
	./build/test_parse

SRCS = test_parse.cpp golden_football.cpp ../components/gameday/espn_parse.cpp ../components/gameday/sport_baseball.cpp ../components/gameday/sport_soccer.cpp ../components/gameday/sport_basketball.cpp ../components/gameday/sport_hockey.cpp

build/test_parse: build/miniz.o $(SRCS) $(wildcard ../components/gameday/*.h) miniz.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) build/miniz.o -o $@

# miniz v1.15 (public domain), the version in the ESP32-S3 ROM that the
# firmware inflates ESPN's gzip replies with.
build/miniz.o: miniz.c
	@mkdir -p build
	$(CC) -O1 -c miniz.c -o $@

# Not part of `all`: replays a folder of recorded single-event documents
# (`.../scoreboard/<event id>`, named ev_<id>_<UTC time>.json) through the
# parse, text and splash code and reports failures and odd splashes.
#   make replay && ./build/replay <folder> <sport: 1 baseball, 2 basketball, 3 hockey, 4 soccer> [name prefix]
replay: build/replay

build/replay: replay.cpp $(filter-out test_parse.cpp golden_football.cpp,$(SRCS)) $(wildcard ../components/gameday/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) replay.cpp $(filter-out test_parse.cpp golden_football.cpp,$(SRCS)) -o $@

clean:
	rm -rf build
.PHONY: all clean replay
