# Unity Test Framework Integration

This directory contains the [Unity](https://github.com/ThrowTheSwitch/Unity) test framework for C, which provides better assertions and test reporting.

## Setup

Unity is vendored here for convenience. To update or install:

```bash
# Download Unity (single-file distribution)
curl -L https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity.c -o unity.c
curl -L https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity.h -o unity.h
curl -L https://raw.githubusercontent.com/ThrowTheSwitch/Unity/master/src/unity_internals.h -o unity_internals.h
```

## Why Unity?

1. **Better Assertions** - `TEST_ASSERT_EQUAL`, `TEST_ASSERT_TRUE`, `TEST_ASSERT_NULL`, etc.
2. **XML/JUnit Output** - Works with CI systems
3. **Test Fixtures** - `setUp()` and `tearDown()` per test
4. **Memory Tracking** - Optional memory leak detection
5. **Classic Mac Compatible** - Pure C, no dependencies

## Usage

```c
#include "unity/unity.h"

void setUp(void) {
    /* Called before each test */
}

void tearDown(void) {
    /* Called after each test */
}

void test_example(void) {
    TEST_ASSERT_EQUAL(42, some_function());
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_TRUE(condition);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_example);
    return UNITY_END();
}
```

## Building with Unity

```makefile
# Add to test build
UNITY_SRC = tests/unity/unity.c
TEST_UNITY_FLAGS = -DUNITY_INCLUDE_PRINT_FORMATTED

test_with_unity: $(TEST_SRC) $(UNITY_SRC)
	$(CC) $(CFLAGS) $(TEST_UNITY_FLAGS) -o $@ $^
```

## Migration Path

Current tests use custom `TEST()/PASS()/FAIL()` macros. To migrate:

1. Replace `TEST("name")` with `void test_name(void)` function
2. Replace `PASS()` with just returning (Unity auto-passes)
3. Replace `FAIL("msg")` with `TEST_FAIL_MESSAGE("msg")`
4. Add `RUN_TEST(test_name)` in main

## Compatibility

Unity works on:
- Linux/macOS (POSIX)
- Classic Mac (68k and PPC)
- Any platform with a C compiler

The framework has no dependencies and can be configured for minimal memory usage on Classic Mac.
