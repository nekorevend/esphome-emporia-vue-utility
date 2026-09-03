# Host unit tests for the emporia_vue_utility ZCL decoder.
# Usage: `make -C test` (or `make test`)

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
INCLUDES := -I ../esphome/components/emporia_vue_utility -I .
BIN := $(CURDIR)/zcltest

.PHONY: test clean
test: $(BIN)
	$(BIN)

$(BIN): test_zcl_meter_reading.cpp doctest.h ../esphome/components/emporia_vue_utility/zcl_meter_reading.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) test_zcl_meter_reading.cpp -o $(BIN)

clean:
	rm -f $(BIN)
