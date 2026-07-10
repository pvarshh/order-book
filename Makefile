CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Werror -pedantic -Iincludes
BUILD_DIR := build

.PHONY: all test clean

all: $(BUILD_DIR)/order_book_tests

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/order_book.o: src/order_book.cpp includes/order-book/order_book.hpp includes/order-book/types.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c src/order_book.cpp -o $@

$(BUILD_DIR)/order_book_tests: $(BUILD_DIR)/order_book.o tests/order_book_tests.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BUILD_DIR)/order_book.o tests/order_book_tests.cpp -o $@

test: $(BUILD_DIR)/order_book_tests
	$(BUILD_DIR)/order_book_tests

clean:
	rm -rf $(BUILD_DIR)
