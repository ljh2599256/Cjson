#include <stdio.h>
#include <string.h>
#include "Cjson.h"
#include <stdlib.h>
static int test_count = 0;
static int test_pass = 0;

#define EXPECT_EQ_BASE(equality, expect, actual, format) \
    do {\
        test_count++;\
        if (equality) {\
            test_pass++;\
        } else {\
            fprintf(stderr, "%s:%d expect: " format " actual: " format "\n", __FILE__, __LINE__, expect, actual);\
        }\
    } while(0)

#define EXPECT_EQ_INT(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%d")
#define EXPECT_EQ_DOUBLE(expect, actual) EXPECT_EQ_BASE((expect) == (actual), expect, actual, "%.16g")
#define EXPECT_EQ_STRING(expect, actual, len) \
    EXPECT_EQ_BASE(sizeof(expect) - 1 == len && memcmp(expect, actual, len) == 0, expect, actual, "%s")
#define EXPECT_TRUE(actual) EXPECT_EQ_BASE((actual) != 0, "true", "false", "%s")
#define EXPECT_FALSE(actual) EXPECT_EQ_BASE((actual) == 0, "false", "true", "%s")

static void test_parse_null() {
    json_value v;
    v.type = JSON_FALSE;
    EXPECT_EQ_INT(JSON_PARSE_OK, json_parse(&v, "null "));
    EXPECT_EQ_INT(JSON_NULL, json_get_type(&v));
}

static void test_parse_true() {
    json_value v;
    v.type = JSON_FALSE;
    EXPECT_EQ_INT(JSON_PARSE_OK, json_parse(&v, " true "));
    EXPECT_EQ_INT(JSON_TRUE, json_get_type(&v));
}

static void test_parse_false() {
    json_value v;
    v.type = JSON_TRUE;
    EXPECT_EQ_INT(JSON_PARSE_OK, json_parse(&v, " false"));
    EXPECT_EQ_INT(JSON_FALSE, json_get_type(&v));
}

#define TEST_NUMBER(expect, json) \
    do {\
        json_value v;\
        EXPECT_EQ_INT(JSON_PARSE_OK, json_parse(&v, json));\
        EXPECT_EQ_DOUBLE(expect, json_get_number(&v));\
        EXPECT_EQ_INT(JSON_NUMBER, json_get_type(&v));\
    } while(0)

static void test_parse_number() {
    TEST_NUMBER(0.0, "0");
    TEST_NUMBER(0.0, "-0");
    TEST_NUMBER(0.0, "-0.0");
    TEST_NUMBER(-1.23, "-1.23");
    TEST_NUMBER(1.0, "1");
    TEST_NUMBER(-1.0, "-1");
    TEST_NUMBER(1.5, "1.5");
    TEST_NUMBER(-1.5, "-1.5");
    TEST_NUMBER(1E10, "1E10");
    TEST_NUMBER(1e10, "1e10");
    TEST_NUMBER(1e+10, "1e+10");
    TEST_NUMBER(1E-10, "1E-10");
    TEST_NUMBER(-1E+10, "-1E+10");
    TEST_NUMBER(-1E-10, "-1E-10");
    TEST_NUMBER(-1.0E+10, "-1.0E+10");
    TEST_NUMBER(-1.23e+10, "-1.23e+10");
    TEST_NUMBER(-1.23E+10, "-1.23E+10");
    TEST_NUMBER(0.0, "1E-10000");
}

#define TEST_STRING(expect, json) \
    do {\
        json_value v;\
        json_init(&v);\
        EXPECT_EQ_INT(JSON_PARSE_OK, json_parse(&v, json));\
        EXPECT_EQ_INT(JSON_STRING, json_get_type(&v));\
        EXPECT_EQ_STRING(expect, json_get_string(&v), json_get_string_length(&v));\
    } while(0)
static void test_parse_string() {
    TEST_STRING("", "\"\"");
    TEST_STRING("hello", "\"hello\"");
    TEST_STRING("Hello\nWorld", "\"Hello\\nWorld\"");
    TEST_STRING("\" \\ / \b \f \n \r \t", "\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"");
}

static void test_access_null() {
    json_value v;
    json_init(&v);

    json_set_string(&v, "", 0);
    json_set_null(&v);
    EXPECT_EQ_INT(JSON_NULL, json_get_type(&v));

    json_free(&v);
}

static void test_access_boolean() {
    json_value v;
    json_init(&v);

    json_set_boolean(&v, 1);
    EXPECT_TRUE(json_get_boolean(&v));
    json_set_boolean(&v, 0);
    EXPECT_FALSE(json_get_boolean(&v));

    json_free(&v);
}

static void test_access_number() {
    json_value v;
    json_init(&v);

    json_set_number(&v, 1.234);
    EXPECT_EQ_DOUBLE(1.234, json_get_number(&v));

    json_free(&v);
}

static void test_access_string() {
    json_value v;
    json_init(&v);

    json_set_string(&v, "", 0);
    EXPECT_EQ_STRING("", json_get_string(&v), json_get_string_length(&v));
    json_set_string(&v, "hello", 5);
    EXPECT_EQ_STRING("hello", json_get_string(&v), json_get_string_length(&v));

    json_free(&v);
}

#define TEST_ERROR(error, json) \
    do {\
        json_value v;\
        v.type = JSON_FALSE;\
        EXPECT_EQ_INT(error, json_parse(&v, json));\
        EXPECT_EQ_INT(JSON_NULL, json_get_type(&v));\
    } while(0)

static void test_parse_invalid_value() {
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "nul");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "?");

#if 1
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "+0");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "+1");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, ".1");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "1.");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "INF");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "inf");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "NAN");
    TEST_ERROR(JSON_PARSE_INVALID_VALUE, "nan");
#endif
}

static void test_parse_expect_value() {
    TEST_ERROR(JSON_PARSE_EXPECT_VALUE, "");
    TEST_ERROR(JSON_PARSE_EXPECT_VALUE, " ");
}

static void test_parse_root_not_singular() {
    TEST_ERROR(JSON_PARSE_ROOT_NOT_SINGULAR, "null x");

    TEST_ERROR(JSON_PARSE_ROOT_NOT_SINGULAR, "0123"); /* after zero should be '.' or nothing */
    TEST_ERROR(JSON_PARSE_ROOT_NOT_SINGULAR, "0x0");
    TEST_ERROR(JSON_PARSE_ROOT_NOT_SINGULAR, "0x123");
}

static void test_parse_number_too_big() {
    TEST_ERROR(JSON_PARSE_NUMBER_TOO_BIG, "1e309");
    TEST_ERROR(JSON_PARSE_NUMBER_TOO_BIG, "-1e309");
}

static void test_parse_invalid_string_escape() {
    TEST_ERROR(JSON_PARSE_INVALID_STRING_ESCAPE, "\"\\v\"");
    TEST_ERROR(JSON_PARSE_INVALID_STRING_ESCAPE, "\"\\'\"");
    TEST_ERROR(JSON_PARSE_INVALID_STRING_ESCAPE, "\"\\0\"");
    TEST_ERROR(JSON_PARSE_INVALID_STRING_ESCAPE, "\"\\x12\"");
}

static void test_parse_invalid_string_char() {
    TEST_ERROR(JSON_PARSE_INVALID_STRING_CHAR, "\"\x01\"");
    TEST_ERROR(JSON_PARSE_INVALID_STRING_CHAR, "\"\x1F\"");
}

#if 0
static void test_parse_invalid_unicode_hex() {
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u0\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u01\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u012\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u/000\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\uG000\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u0/00\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u0G00\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u00/0\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u00G0\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u000/\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u000G\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_HEX, "\"\\u 123\"");
}

static void test_parse_invalid_unicode_surrogate() {
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uDBFF\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\\\\\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\\uDBFF\"");
    TEST_ERROR(JSON_PARSE_INVALID_UNICODE_SURROGATE, "\"\\uD800\\uE000\"");
}
#endif

static void json_test_parse() {
    test_parse_null();
    test_parse_true();
    test_parse_false();
    test_parse_number();
    test_parse_string();
    
    test_access_null();
    test_access_boolean();
    test_access_number();
    test_access_string();

    test_parse_expect_value();
    test_parse_invalid_value();
    test_parse_root_not_singular();
    test_parse_number_too_big();
    test_parse_invalid_string_escape();
    test_parse_invalid_string_char();
}

int main() {
    json_test_parse();
    printf("%d/%d (%3.2f%%) passed\n", test_pass, test_count, test_pass*100.0 / test_count);
    return 0;
}
