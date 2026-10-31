ARDUINOJSON ?= ArduinoJson/src
# -Wno-maybe-uninitialized: GCC false positive inside ArduinoJson when built with the sanitizers.
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -Wno-maybe-uninitialized -O1 -g
SOURCES = test_main.cpp $(wildcard ../../src/wbskt/*.cpp)

test: host-tests
	./host-tests

host-tests: $(SOURCES) $(wildcard ../../src/wbskt/*.h)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -isystem $(ARDUINOJSON) $(SOURCES) -o $@

clean:
	rm -f host-tests

.PHONY: test clean
