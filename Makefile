ifeq ($(origin CXX),default)
CXX := clang++
endif

BUILD_DIR ?= build
CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -Werror -g
SANITIZER_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

UNIT_TEST := $(BUILD_DIR)/tests/test_borrowfence
LEAK_CASES_TEST := $(BUILD_DIR)/tests/leak_cases
DEATH_TEST := $(BUILD_DIR)/tests/active_borrow_abort
ASAN_TEST := $(BUILD_DIR)/tests/test_borrowfence_asan
ASAN_LEAK_CASES_TEST := $(BUILD_DIR)/tests/leak_cases_asan
RAW_LEAK_TEST := $(BUILD_DIR)/tests/lsan_raw_leak

EXAMPLE_SOURCES := $(wildcard examples/*.cpp)
EXAMPLES := $(patsubst examples/%.cpp,$(BUILD_DIR)/examples/%,$(EXAMPLE_SOURCES))

.PHONY: all test asan leak-tests examples clean

all: test examples

test: $(UNIT_TEST) $(LEAK_CASES_TEST) $(DEATH_TEST)
	$(UNIT_TEST)
	$(LEAK_CASES_TEST)
	@if $(DEATH_TEST) >$(BUILD_DIR)/tests/active_borrow_abort.log 2>&1; then \
		echo "expected active-borrow destruction test to fail"; \
		exit 1; \
	else \
		echo "active-borrow destruction test failed as expected"; \
	fi

asan: $(ASAN_TEST) $(ASAN_LEAK_CASES_TEST)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 $(ASAN_TEST)
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 $(ASAN_LEAK_CASES_TEST)

leak-tests: $(LEAK_CASES_TEST) $(RAW_LEAK_TEST)
	$(LEAK_CASES_TEST)
	@if ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 LSAN_OPTIONS=exitcode=23 $(RAW_LEAK_TEST) >$(BUILD_DIR)/tests/lsan_raw_leak.log 2>&1; then \
		echo "expected LeakSanitizer to report the raw leak"; \
		exit 1; \
	else \
		echo "LeakSanitizer reported the intentional raw leak as expected"; \
	fi

examples: $(EXAMPLES)

$(UNIT_TEST): tests/test_borrowfence.cpp include/borrowfence/borrowfence.hpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(LEAK_CASES_TEST): tests/leak_cases.cpp include/borrowfence/borrowfence.hpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(DEATH_TEST): tests/active_borrow_abort.cpp include/borrowfence/borrowfence.hpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(ASAN_TEST): tests/test_borrowfence.cpp include/borrowfence/borrowfence.hpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SANITIZER_FLAGS) $< -o $@

$(ASAN_LEAK_CASES_TEST): tests/leak_cases.cpp include/borrowfence/borrowfence.hpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SANITIZER_FLAGS) $< -o $@

$(RAW_LEAK_TEST): tests/lsan_raw_leak.cpp | $(BUILD_DIR)/tests
	$(CXX) $(CXXFLAGS) $(SANITIZER_FLAGS) $< -o $@

$(BUILD_DIR)/examples/%: examples/%.cpp include/borrowfence/borrowfence.hpp | $(BUILD_DIR)/examples
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/tests $(BUILD_DIR)/examples:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
