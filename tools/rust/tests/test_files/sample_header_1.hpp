#ifndef SAMPLE_TEST_HEADER_1_H
#define SAMPLE_TEST_HEADER_1_H

#include <array>
#include <string>
#include <vector>

#define AN_EXAMPLE_LEN_1 10
#define AN_EXAMPLE_LEN_2 15

// A simple struct to test primitive types
struct PrimitiveTypes {
  int intField;
  float floatField;
  char charField;
  bool boolField[2];
};

// A struct with embedded structs
struct EmbeddedStruct {
  int64_t id;
  PrimitiveTypes basicFields; // Nested struct
  char teststring1[AN_EXAMPLE_LEN_1];
  char teststring2[AN_EXAMPLE_LEN_2];
};

#endif // SAMPLE_TEST_HEADER_1_H
