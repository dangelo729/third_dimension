# SPDX-License-Identifier: MIT
#
# host test harness for ThirdDimension: pure clang++, no Arduino, no
# hardware. `make` builds td_host_tests; `make run` builds and executes it.
#
# -ffp-contract=off is load-bearing: the bit-exact golden pins (dither tile
# signatures, full-frame render hashes) depend on the exact floating-point
# expression shapes in TD_Math.h and the span contract — FMA contraction
# changes last-ulp results and produces false golden failures.
#
# the suite is the library's acceptance gate.

CXX ?= clang++
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -ffp-contract=off
CPPFLAGS := -I../../src

CORE_SRC := $(wildcard ../../src/*.cpp) $(wildcard ../../src/internal/*.cpp)
TEST_SRC := $(wildcard test_*.cpp)
HDRS := $(wildcard ../../src/*.h) $(wildcard ../../src/internal/*.h) \
        $(wildcard ../../src/models/*.h) test_common.h \
        TD_Model_ExportFixture.h TD_Model_DecimatedFixture.h

td_host_tests: $(CORE_SRC) $(TEST_SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(CORE_SRC) $(TEST_SRC) -o $@

.PHONY: run clean

run: td_host_tests
	./td_host_tests

clean:
	rm -f td_host_tests
