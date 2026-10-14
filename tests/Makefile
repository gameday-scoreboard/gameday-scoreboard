# Host tests for the ESPN parser. Needs g++ and the ArduinoJson single header
# next to this file (ArduinoJson.h, version 7.4.3 to match ESPHome).
CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -O1 -Wall -Wextra -I../components/gameday -I.

all: build/test_parse
	./build/test_parse

build/test_parse: build/miniz.o test_parse.cpp ../components/gameday/espn_parse.cpp ../components/gameday/espn_parse.h ../components/gameday/favorites.h ../components/gameday/espn_parse_impl.h ../components/gameday/startup.h ../components/gameday/poll_timing.h ../components/gameday/logo_lru.h ../components/gameday/recap.h ../components/gameday/timezones.h ../components/gameday/demo.h ../components/gameday/teams.h ../components/gameday/team_colors.h ../components/gameday/gzip_stream.h miniz.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) test_parse.cpp ../components/gameday/espn_parse.cpp build/miniz.o -o $@

# miniz v1.15 (public domain), the version in the ESP32-S3 ROM that the
# firmware inflates ESPN's gzip replies with.
build/miniz.o: miniz.c
	@mkdir -p build
	$(CC) -O1 -c miniz.c -o $@

clean:
	rm -rf build
.PHONY: all clean
