# Host tests for the ESPN parser. Needs g++ and the ArduinoJson single header
# next to this file (ArduinoJson.h, version 7.4.3 to match ESPHome).
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -Wall -Wextra -I../components/gameday -I.
# Link the GCC runtime in on Windows. Git Bash puts its own older
# libstdc++-6.dll ahead of the compiler's on PATH, and the mismatched
# runtime faults on the first ifstream.
ifeq ($(OS),Windows_NT)
LDFLAGS += -static
endif

all: build/test_parse
	./build/test_parse

build/test_parse: test_parse.cpp ../components/gameday/espn_parse.cpp ../components/gameday/espn_parse.h ../components/gameday/favorites.h ../components/gameday/espn_parse_impl.h ../components/gameday/startup.h ../components/gameday/poll_timing.h ../components/gameday/logo_lru.h ../components/gameday/recap.h ../components/gameday/timezones.h ../components/gameday/demo.h ../components/gameday/teams.h ../components/gameday/team_colors.h ../components/gameday/panel_text.h ../components/gameday/auto_update.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) test_parse.cpp ../components/gameday/espn_parse.cpp -o $@ $(LDFLAGS)

clean:
	rm -rf build
.PHONY: all clean
