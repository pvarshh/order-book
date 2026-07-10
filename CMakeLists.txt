cmake_minimum_required(VERSION 3.20)

project(order_book LANGUAGES CXX)

add_library(order_book src/order_book.cpp)
target_include_directories(order_book PUBLIC includes)
target_compile_features(order_book PUBLIC cxx_std_20)
target_compile_options(order_book PRIVATE -Wall -Wextra -Werror -pedantic)

enable_testing()

add_executable(order_book_tests tests/order_book_tests.cpp)
target_link_libraries(order_book_tests PRIVATE order_book)
target_compile_options(order_book_tests PRIVATE -Wall -Wextra -Werror -pedantic)

add_test(NAME order_book_tests COMMAND order_book_tests)
