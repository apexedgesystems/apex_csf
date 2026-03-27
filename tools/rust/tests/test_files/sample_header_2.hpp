#ifndef SAMPLE_TEST_HEADER_2_H
#define SAMPLE_TEST_HEADER_2_H

#include <array>
#include <string>
#include <vector>

#define AN_EXAMPLE_LEN 10

// A struct to test std::array
struct FixedArrayStruct {
  char description[AN_EXAMPLE_LEN]; // Requires max_size definition
  std::array<int, 5> fixedArray;    // Requires storage_order definition
};

#endif // SAMPLE_TEST_HEADER_2_H
