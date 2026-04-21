CXX           := clang++
CXXFLAGS      := -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
INCLUDES      := -Iinclude -Ithird_party -Ithird_party/cpp-httplib
LDFLAGS       :=

GTEST_VERSION := 1.16.0
GTEST_URL     := https://github.com/google/googletest/archive/refs/tags/v$(GTEST_VERSION).tar.gz
HTTPLIB_URL   := https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h

GTEST_INC     := -Ithird_party/googletest/googletest/include -Ithird_party/googletest/googletest

LIB_SRCS      := $(wildcard src/*.cpp)
LIB_OBJS      := $(patsubst src/%.cpp,build/obj/%.o,$(LIB_SRCS))

TEST_SRCS     := $(wildcard tests/*.cpp)
TEST_OBJS     := $(patsubst tests/%.cpp,build/obj/tests/%.o,$(TEST_SRCS))

GTEST_OBJ     := build/obj/gtest-all.o
GTEST_MAIN_OBJ:= build/obj/gtest_main.o

.PHONY: all clean distclean deps test bench cli server

all: deps cli server bench

# --- Dependencies ---
deps: third_party/googletest third_party/cpp-httplib/httplib.h

third_party/googletest:
	@mkdir -p third_party
	@echo "Downloading GoogleTest v$(GTEST_VERSION)..."
	@curl -sL $(GTEST_URL) -o third_party/gtest.tar.gz
	@cd third_party && tar xzf gtest.tar.gz
	@mv third_party/googletest-$(GTEST_VERSION) third_party/googletest
	@rm third_party/gtest.tar.gz

third_party/cpp-httplib/httplib.h:
	@mkdir -p third_party/cpp-httplib
	@echo "Downloading cpp-httplib..."
	@curl -sL $(HTTPLIB_URL) -o third_party/cpp-httplib/httplib.h

# --- Build directories ---
build/obj:
	@mkdir -p build/obj

build/obj/tests:
	@mkdir -p build/obj/tests

# --- Library objects ---
build/obj/%.o: src/%.cpp | build/obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- CLI ---
cli: deps build/venus-cli

build/venus-cli: build/obj/cli_main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/obj/cli_main.o: cmd/cli_main.cpp | build/obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Server ---
server: deps build/venus-server

build/venus-server: build/obj/server_main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/obj/server_main.o: cmd/server_main.cpp | build/obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Benchmark ---
bench: deps build/venus-bench

build/venus-bench: build/obj/bench_main.o $(LIB_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/obj/bench_main.o: cmd/bench_main.cpp | build/obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# --- Tests ---
test: deps build/venus-tests
	@echo "Running tests..."
	@./build/venus-tests

build/venus-tests: $(TEST_OBJS) $(LIB_OBJS) $(GTEST_OBJ) $(GTEST_MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) -lpthread

build/obj/tests/%.o: tests/%.cpp | build/obj/tests deps
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_INC) -c $< -o $@

$(GTEST_OBJ): third_party/googletest/googletest/src/gtest-all.cc | build/obj deps
	$(CXX) $(CXXFLAGS) -isystem third_party/googletest/googletest/include \
	  -Ithird_party/googletest/googletest -c $< -o $@

$(GTEST_MAIN_OBJ): third_party/googletest/googletest/src/gtest_main.cc | build/obj deps
	$(CXX) $(CXXFLAGS) -isystem third_party/googletest/googletest/include \
	  -Ithird_party/googletest/googletest -c $< -o $@

# --- Clean ---
clean:
	rm -rf build

distclean: clean
	rm -rf third_party
