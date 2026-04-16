#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../include/xon_api.h"

static void test_parse_core_features(void) {
    const char* input =
        "{\n"
        "  // Core features\n"
        "  name: \"Xon\",\n"
        "  hex: 0x10,\n"
        "  enabled: true,\n"
        "  values: [1, 2, 3,],\n"
        "  nested: { ok: null, },\n"
        "}\n";
    XonValue* root = xonify_string(input);
    assert(root != NULL);
    assert(xon_is_object(root));
    assert(xon_object_size(root) == 5);

    {
        XonValue* name = xon_object_get(root, "name");
        assert(xon_is_string(name));
        assert(strcmp(xon_get_string(name), "Xon") == 0);
    }
    {
        XonValue* hex = xon_object_get(root, "hex");
        assert(xon_is_number(hex));
        assert((int)xon_get_number(hex) == 16);
    }
    {
        XonValue* values = xon_object_get(root, "values");
        assert(xon_is_list(values));
        assert(xon_list_size(values) == 3);
        assert((int)xon_get_number(xon_list_get(values, 0)) == 1);
        assert((int)xon_get_number(xon_list_get(values, 2)) == 3);
    }
    {
        XonValue* nested = xon_object_get(root, "nested");
        assert(xon_is_object(nested));
        assert(xon_is_null(xon_object_get(nested, "ok")));
    }

    xon_free(root);
}

static void test_round1_expression_semantics(void) {
    XonValue* root = xonify_string(
        "{\n"
        "  precedence: 1 + 2 * 3 - 4 / 2,\n"
        "  grouped: (1 + 2) * (3 - 1),\n"
        "  bool_chain: true && false || true,\n"
        "  compare: 1 + 2 == 3 && 4 != 5 && 2 < 4,\n"
        "  not_expr: !false,\n"
        "  null_coalescing: null ?? 100,\n"
        "}\n"
    );
    XonValue* evaluated;
    XonValue* value;

    assert(root != NULL);
    evaluated = xon_eval(root);
    assert(evaluated != NULL);

    value = xon_object_get(evaluated, "precedence");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 5);

    value = xon_object_get(evaluated, "grouped");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 6);

    value = xon_object_get(evaluated, "bool_chain");
    assert(value && xon_is_bool(value) && xon_get_bool(value) == 1);

    value = xon_object_get(evaluated, "compare");
    assert(value && xon_is_bool(value) && xon_get_bool(value) == 1);

    value = xon_object_get(evaluated, "not_expr");
    assert(value && xon_is_bool(value) && xon_get_bool(value) == 1);

    value = xon_object_get(evaluated, "null_coalescing");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 100);

    xon_free(evaluated);
    xon_free(root);
}

static void test_expression_identifiers_evaluated(void) {
    XonValue* root = xonify_string("{ let a = 1, b: a + 1 }");
    XonValue* evaluated;
    XonValue* value;

    assert(root != NULL);
    evaluated = xon_eval(root);
    assert(evaluated != NULL);
    value = xon_object_get(evaluated, "b");
    assert(xon_is_number(value));
    assert((int)xon_get_number(value) == 2);

    xon_free(evaluated);
    xon_free(root);
}

static void test_forward_references(void) {
    XonValue* root = xonify_string("{ let a = b + 1, let b = 2, result: a + b }");
    XonValue* evaluated;
    XonValue* value;

    assert(root != NULL);
    evaluated = xon_eval(root);
    assert(evaluated != NULL);
    value = xon_object_get(evaluated, "result");
    assert(xon_is_number(value));
    assert((int)xon_get_number(value) == 5);

    xon_free(evaluated);
    xon_free(root);
}

static void test_conditionals_and_truthiness(void) {
    XonValue* root = xonify_string(
        "{\n"
        "  short_and: false && (1 / 0),\n"
        "  short_or: true || (1 / 0),\n"
        "  ternary: if (1 < 2) \"yes\" else \"no\",\n"
        "  nullish: null ?? 7,\n"
        "}\n"
    );
    XonValue* evaluated;
    XonValue* value;

    assert(root != NULL);
    evaluated = xon_eval(root);
    assert(evaluated != NULL);

    value = xon_object_get(evaluated, "short_and");
    assert(value && xon_is_bool(value) && xon_get_bool(value) == 0);
    value = xon_object_get(evaluated, "short_or");
    assert(value && xon_is_bool(value) && xon_get_bool(value) == 1);
    value = xon_object_get(evaluated, "ternary");
    assert(value && xon_is_string(value) && strcmp(xon_get_string(value), "yes") == 0);
    value = xon_object_get(evaluated, "nullish");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 7);

    xon_free(evaluated);
    xon_free(root);
}

static void test_functions_and_closures(void) {
    XonValue* root = xonify_string(
        "{\n"
        "  let base = 10,\n"
        "  total: make(2, 3),\n"
        "  let make = (a, b) => a + b + base,\n"
        "}\n"
    );
    XonValue* evaluated;
    XonValue* value;

    assert(root != NULL);
    evaluated = xon_eval(root);
    assert(evaluated != NULL);

    value = xon_object_get(evaluated, "total");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 15);

    xon_free(evaluated);
    xon_free(root);
}

static void test_builtins(void) {
    XonValue* root;
    XonValue* evaluated;
    XonValue* value;

    setenv("XON_TEST_EVAL", "present", 1);

    root = xonify_string(
        "{\n"
        "  abs_val: abs(-7),\n"
        "  max_val: max(1, 9, 4),\n"
        "  min_val: min(9, 4, 12),\n"
        "  len_text: len(\"abc\"),\n"
        "  len_list: len([1, 2, 3]),\n"
        "  upper_text: upper(\"abC\"),\n"
        "  lower_text: lower(\"abC\"),\n"
        "  str_text: str(42),\n"
        "  has_key: has({a:1,b:2}, \"b\"),\n"
        "  has_missing: has({a:1,b:2}, \"x\"),\n"
        "  env_val: env(\"XON_TEST_EVAL\"),\n"
        "  list_keys: keys({first:1, second:2}),\n"
        "}\n"
    );

    assert(root != NULL);
    evaluated = xon_eval(root);
    assert(evaluated != NULL);

    value = xon_object_get(evaluated, "abs_val");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 7);
    value = xon_object_get(evaluated, "max_val");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 9);
    value = xon_object_get(evaluated, "min_val");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 4);
    value = xon_object_get(evaluated, "len_text");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 3);
    value = xon_object_get(evaluated, "len_list");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 3);
    value = xon_object_get(evaluated, "upper_text");
    assert(value && xon_is_string(value) && strcmp(xon_get_string(value), "ABC") == 0);
    value = xon_object_get(evaluated, "lower_text");
    assert(value && xon_is_string(value) && strcmp(xon_get_string(value), "abc") == 0);
    value = xon_object_get(evaluated, "str_text");
    assert(value && xon_is_string(value) && strcmp(xon_get_string(value), "42") == 0);
    value = xon_object_get(evaluated, "has_key");
    assert(value && xon_is_bool(value) && xon_get_bool(value) == 1);
    value = xon_object_get(evaluated, "has_missing");
    assert(value && xon_is_bool(value) && xon_get_bool(value) == 0);
    value = xon_object_get(evaluated, "env_val");
    assert(value && xon_is_string(value) && strcmp(xon_get_string(value), "present") == 0);

    value = xon_object_get(evaluated, "list_keys");
    assert(value && xon_is_list(value) && xon_list_size(value) == 2);
    value = xon_list_get(value, 0);
    assert(value && xon_is_string(value) && strcmp(xon_get_string(value), "first") == 0);

    xon_free(evaluated);
    xon_free(root);
}

static void test_identifier_reference_and_comments(void) {
    XonValue* root;
    XonValue* evaluated;
    XonValue* value;

    setenv("XON_TEST_ENV_VALUE", "round1", 1);

    root = xonify_string(
        "{\n"
        "  // line comment\n"
        "  from_env: XON_TEST_ENV_VALUE,\n"
        "  # inline hash comment\n"
        "  let local = 40 + 2,\n"
        "  /* block comment */\n"
        "  via_add: local + 1,\n"
        "}\n"
    );

    assert(root != NULL);
    evaluated = xon_eval(root);
    assert(evaluated != NULL);

    value = xon_object_get(evaluated, "from_env");
    assert(value && xon_is_string(value));
    assert(strcmp(xon_get_string(value), "round1") == 0);

    value = xon_object_get(evaluated, "via_add");
    assert(value && xon_is_number(value) && (int)xon_get_number(value) == 43);

    xon_free(evaluated);
    xon_free(root);
}

static void test_function_arity_and_call_failures(void) {
    XonValue* root;

    root = xonify_string(
        "{\n"
        "  let add = (a, b) => a + b,\n"
        "  ok: add(2, 3),\n"
        "}\n"
    );
    assert(root != NULL);
    assert(xon_eval(root) != NULL);
    xon_free(root);

    root = xonify_string(
        "{\n"
        "  let add = (a, b) => a + b,\n"
        "  bad_few: add(1),\n"
        "}\n"
    );
    assert(root != NULL);
    assert(xon_eval(root) == NULL);
    xon_free(root);

    root = xonify_string(
        "{\n"
        "  let add = (a, b) => a + b,\n"
        "  bad_many: add(1, 2, 3),\n"
        "}\n"
    );
    assert(root != NULL);
    assert(xon_eval(root) == NULL);
    xon_free(root);

    root = xonify_string(
        "{\n"
        "  let add = (a, b) => a + b,\n"
        "  bad_many = add(1)\n"
        "}\n"
    );
    assert(root == NULL);
}

static void test_object_iteration(void) {
    XonValue* root = xonify_string("{ first: 1, second: 2, third: 3 }");
    assert(root != NULL);
    assert(xon_object_size(root) == 3);
    assert(strcmp(xon_object_key_at(root, 0), "first") == 0);
    assert(strcmp(xon_object_key_at(root, 1), "second") == 0);
    assert((int)xon_get_number(xon_object_value_at(root, 2)) == 3);
    xon_free(root);
}

static void test_serialization(void) {
    XonValue* root = xonify_string("{ name: \"A\", list: [1, 2] }");
    char* json;
    char* xon;
    assert(root != NULL);

    json = xon_to_json(root, 1);
    xon = xon_to_xon(root, 1);
    assert(json != NULL);
    assert(xon != NULL);
    assert(strstr(json, "\"name\": \"A\"") != NULL);
    assert(strstr(xon, "name: \"A\"") != NULL);

    xon_string_free(json);
    xon_string_free(xon);
    xon_free(root);
}

static void test_json_input_supported(void) {
    XonValue* root = xonify_string("{\"name\":\"JsonInput\",\"count\":2}");
    assert(root != NULL);
    assert(xon_is_string(xon_object_get(root, "name")));
    assert((int)xon_get_number(xon_object_get(root, "count")) == 2);
    xon_free(root);
}

int main(void) {
    printf("=== Xon Test Suite ===\n");
    test_parse_core_features();
    test_round1_expression_semantics();
    test_expression_identifiers_evaluated();
    test_forward_references();
    test_conditionals_and_truthiness();
    test_functions_and_closures();
    test_identifier_reference_and_comments();
    test_function_arity_and_call_failures();
    test_builtins();
    test_object_iteration();
    test_serialization();
    test_json_input_supported();
    printf("All tests passed.\n");
    return 0;
}
