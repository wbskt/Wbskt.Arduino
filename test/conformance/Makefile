# Builds the conformance harness: the SDK's own sources on Linux, over the shims in shim/.
# make -C test/conformance ARDUINOJSON=<path to ArduinoJson/src>
ARDUINOJSON ?= ../host/ArduinoJson/src
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wno-maybe-uninitialized -O1 -g
LDFLAGS += -pthread
DEFINES = -DARDUINOJSON_ENABLE_ARDUINO_STRING=1
SOURCES = harness.cpp shim/Host.cpp shim/WebSocketsShim.cpp shim/OtaFlash.cpp ../../src/WbsktClient.cpp $(wildcard ../../src/wbskt/*.cpp)
HEADERS = $(wildcard shim/*.h shim/freertos/*.h ../../src/*.h ../../src/wbskt/*.h)

wbskt-harness: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(DEFINES) -Ishim -I../../src -isystem $(ARDUINOJSON) $(SOURCES) $(LDFLAGS) -o $@

# The same harness over the real links2004/arduinoWebSockets instead of the stand-in in
# shim/WebSocketsClient.h: only the ESP32 core's WiFiClient is shimmed (shim/real). Built with
# AddressSanitizer, so a network client the library leaks (begin() over one still allocated) fails
# the run at exit. make real WEBSOCKETS=<path to arduinoWebSockets/src>
WEBSOCKETS ?= arduinoWebSockets/src
REAL_SOURCES = harness.cpp shim/Host.cpp shim/OtaFlash.cpp shim/real/WiFiClient.cpp ../../src/WbsktClient.cpp $(wildcard ../../src/wbskt/*.cpp) \
	$(WEBSOCKETS)/WebSockets.cpp $(WEBSOCKETS)/WebSocketsClient.cpp
REAL_C_SOURCES = $(WEBSOCKETS)/libsha1/libsha1.c $(WEBSOCKETS)/libb64/cencode.c $(WEBSOCKETS)/libb64/cdecode.c
REAL_FLAGS = -std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer -DESP32 -DESP_IDF_VERSION_MAJOR=5 \
	-DNODEBUG_WEBSOCKETS $(DEFINES)

real: wbskt-harness-real

wbskt-harness-real: $(REAL_SOURCES) $(HEADERS) $(wildcard shim/real/*.h shim/real/sha/*.h)
	$(CC) -O1 -g -fsanitize=address -c $(REAL_C_SOURCES)
	$(CXX) $(REAL_FLAGS) -include shim/real/real.h -Ishim/real -I$(WEBSOCKETS) -Ishim -I../../src -isystem $(ARDUINOJSON) \
		$(REAL_SOURCES) libsha1.o cencode.o cdecode.o $(LDFLAGS) -fsanitize=address -o $@
	rm -f libsha1.o cencode.o cdecode.o

clean:
	rm -f wbskt-harness wbskt-harness-real

.PHONY: clean real
