CXX      := g++
BASE_FLAGS := -std=c++17 -Wall -Wextra -Wpedantic

SRCS     := $(wildcard src/**/*.cpp) src/main.cpp
REL_OBJS := $(SRCS:src/%.cpp=build/release/%.o)
DBG_OBJS := $(SRCS:src/%.cpp=build/debug/%.o)

TEST_SRCS := $(wildcard test/*.cpp)
TEST_OBJS := $(TEST_SRCS:test/%.cpp=build/debug/test/%.o)
BENCH_SRCS := $(wildcard bench/*.cpp)
BENCH_OBJS := $(BENCH_SRCS:bench/%.cpp=build/release/bench/%.o)

.PHONY: release debug test bench lint format clean

release: CXXFLAGS := $(BASE_FLAGS) -O2 -march=native -DNDEBUG
release: silo

debug: CXXFLAGS := $(BASE_FLAGS) -O0 -g -fsanitize=address,undefined
debug: silo-dbg

silo: $(REL_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

silo-dbg: $(DBG_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

build/release/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/debug/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/debug/test/%.o: test/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/release/bench/%.o: bench/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test/runner: CXXFLAGS := $(BASE_FLAGS) -O0 -g -fsanitize=address,undefined
test/runner: $(filter-out build/debug/main.o, $(DBG_OBJS)) $(filter-out build/debug/test/test_main.o, $(TEST_OBJS)) build/debug/test/test_main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

bench/runner: CXXFLAGS := $(BASE_FLAGS) -O2 -march=native -DNDEBUG
bench/runner: $(filter-out src/main.o, $(REL_OBJS)) $(BENCH_OBJS) build/release/bench/bench_main.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test: test/runner
	LD_PRELOAD=$$(g++ -print-file-name=libasan.so) ./test/runner

bench: bench/runner
	./bench/runner

lint:
	clang-tidy src/ -- -std=c++17
	clang-format --dry-run -Werror src/**/*.cpp src/**/*.h

format:
	clang-format -i src/**/*.cpp src/**/*.h

clean:
	rm -rf build silo silo-dbg test/runner bench/runner
