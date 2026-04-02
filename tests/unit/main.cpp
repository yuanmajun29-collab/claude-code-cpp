// Main entry point for all unit tests
// This file should be compiled with GoogleTest and link all test objects

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Set up test environment
    ::testing::GTEST_FLAG(print_time) = true;
    ::testing::GTEST_FLAG(color) = "yes";
    
    return RUN_ALL_TESTS();
}
