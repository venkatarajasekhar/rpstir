#include <stdbool.h>
#include <stdlib.h>

#include "test/unittest.h"


static bool test_pass(
    void)
{
    TEST(int,
         "%d",
         5 % 10,
         ==, 5);
    TEST(int,
         "%d",
         9 % 5,
         ==, 4);
    TEST(int,
         "%d",
         1,
         !=,
         2);
    TEST(int,
         "%d",
         1,
         <,
         2);
    TEST(int,
         "%d",
         2,
         <=,
         2);
    TEST(int,
         "%d",
         2,
         >=,
         2);
    TEST(int,
         "%d",
         2,
         >,
         1);
    TEST_BOOL(true || true, true);
    TEST_BOOL(false && false, false);
    TEST_STR("", ==, "");
    TEST_STR("foo", ==, "foo");
    TEST_STR("foo", !=, "bar");
    TEST_STR("a", <, "b");
    TEST_STR("a", <=, "b");
    TEST_STR("a", <=, "a");
    TEST_STR("b", >, "a");
    TEST_STR("b", >=, "a");
    TEST_STR("b", >=, "b");
    return true;
}

static bool test_modulo_fail(
    void)
{
    TEST(int,
         "%d",
         5 % 7,
         <,
         0);
    return true;
}

static bool test_bool_true_fail(
    void)
{
    TEST_BOOL(false || true, false);
    return true;
}

static bool test_bool_false_fail(
    void)
{
    TEST_BOOL(true && false, true);
    return true;
}

static bool test_str_fail(
    void)
{
    TEST_STR("baz", ==, "quux");
    return true;
}

int main(
    void)
{
    if (!test_pass())
        return EXIT_FAILURE;
    if (test_modulo_fail())
        return EXIT_FAILURE;
    if (test_bool_true_fail())
        return EXIT_FAILURE;
    if (test_bool_false_fail())
        return EXIT_FAILURE;
    if (test_str_fail())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
