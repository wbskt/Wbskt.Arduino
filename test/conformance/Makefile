# Builds the conformance harness: the SDK's own sources on Linux, over the shims in shim/.
# make -C test/conformance ARDUINOJSON=<path to ArduinoJson/src>
ARDUINOJSON ?= ../host/ArduinoJson/src
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wno-maybe-uninitialized -O1 -g
LDFLAGS += -pthread
DEFINES = -DARDUINOJSON_ENABLE_ARDUINO_STRING=1
SOURCES = harness.cpp shim/Host.cpp ../../src/WbsktClient.cpp $(wildcard ../../src/wbskt/*.cpp)
HEADERS = $(wildcard shim/*.h shim/freertos/*.h ../../src/*.h ../../src/wbskt/*.h)

wbskt-harness: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(DEFINES) -Ishim -I../../src -isystem $(ARDUINOJSON) $(SOURCES) $(LDFLAGS) -o $@

clean:
	rm -f wbskt-harness

.PHONY: clean
