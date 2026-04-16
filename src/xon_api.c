#include "../include/xon_api.h"
#include "lexer.h"
#include "logger.h"

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "xon.c"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

typedef struct EvalScope EvalScope;

typedef struct EvalBinding EvalBinding;

typedef struct RuntimeFunction {
    int is_native;
    size_t arity_min;
    size_t arity_max;
    union {
        DataNode* (*native)(size_t, const DataNode* const*, void*);
        struct {
            DataNode* params;
            DataNode* body;
            EvalScope* closure;
        } user;
    } impl;
    int ref_count;
    void* userdata;
} RuntimeFunction;

struct EvalScope {
    struct EvalScope* parent;
    struct EvalBinding* first;
    int ref_count;
};

struct EvalBinding {
    char* name;
    int is_const;
    int initialized;
    int resolving;
    DataNode* init_expr;
    DataNode* value;
    RuntimeFunction* function;
    struct EvalBinding* next;
};

typedef struct {
    int active;
    char message[512];
} EvalError;

typedef DataNode* (*BuiltinFn)(size_t argc, const DataNode* const* argv, void* userdata);

typedef struct {
    const char* name;
    BuiltinFn impl;
    int is_variadic;
    size_t arity_min;
    size_t arity_max;
} BuiltinSpec;

static EvalBinding* eval_scope_find_binding(EvalScope* scope, const char* name);
static EvalScope* eval_scope_new(EvalScope* parent);
static void eval_scope_release(EvalScope* scope);
static int eval_is_identifier(const char* key);
static size_t eval_list_size(const DataNode* list);
static char* clone_c_string(const char* src);
static DataNode* clone_data_node(const DataNode* src);
static DataNode* eval_lookup_identifier(const char* name, EvalScope* scope, EvalError* err);
static DataNode* eval_object_node(const DataNode* node, EvalScope* scope, EvalError* err);
static DataNode* eval_list_node(const DataNode* node, EvalScope* scope, EvalError* err);
static DataNode* eval_expr_node(const XonExpr* expr, EvalScope* scope, EvalError* err);
static DataNode* xon_eval_node(const DataNode* node, EvalScope* scope, EvalError* err);
static DataNode* eval_call(RuntimeFunction* fn, size_t argc, DataNode* const* argv, EvalError* err);
static int eval_register_builtin(EvalScope* scope, const BuiltinSpec* spec, EvalError* err);
static EvalScope* eval_create_global_scope(EvalError* err);

static void eval_set_error(EvalError* err, const char* msg) {
    if (!err || !msg) return;
    err->active = 1;
    snprintf(err->message, sizeof(err->message), "%s", msg);
}

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

static DataNode* xon_get_key_internal(DataNode* obj, const char* key) {
    DataNode* current;
    if (!obj || obj->type != TYPE_OBJECT || !key) return NULL;

    current = obj->data.aggregate.value;
    while (current) {
        if (current->type != TYPE_OBJECT) {
            current = current->next;
            continue;
        }

        if (current->data.aggregate.key &&
            current->data.aggregate.key->type == TYPE_STRING &&
            current->data.aggregate.key->data.s_val &&
            strcmp(current->data.aggregate.key->data.s_val, key) == 0) {
            return current->data.aggregate.value;
        }
        current = current->next;
    }
    return NULL;
}

static void print_ast(const DataNode* node, int depth) {
    int i;
    const DataNode* current;
    const DataNode* item;

    if (!node) return;
    for (i = 0; i < depth; i++) printf("  ");

    switch (node->type) {
        case TYPE_OBJECT:
            printf("OBJECT\n");
            current = node->data.aggregate.value;
            while (current) {
                for (i = 0; i < depth + 1; i++) printf("  ");
                if (current->data.aggregate.key && current->data.aggregate.key->data.s_val) {
                    printf("Key: %s\n", current->data.aggregate.key->data.s_val);
                } else {
                    printf("Key: <invalid>\n");
                }
                print_ast(current->data.aggregate.value, depth + 2);
                current = current->next;
            }
            break;
        case TYPE_LIST:
            printf("LIST\n");
            item = node->data.aggregate.value;
            while (item) {
                print_ast(item, depth + 1);
                item = item->next;
            }
            break;
        case TYPE_STRING:
            printf("STRING: \"%s\"\n", node->data.s_val ? node->data.s_val : "");
            break;
        case TYPE_NUMBER:
            printf("NUMBER: %.17g\n", node->data.n_val);
            break;
        case TYPE_BOOL:
            printf("BOOL: %s\n", node->data.b_val ? "true" : "false");
            break;
        case TYPE_NULL:
            printf("NULL\n");
            break;
        case TYPE_EXPR:
            printf("EXPR\n");
            break;
        case TYPE_DECL:
            printf("%s\n", node->data.declaration.is_const ? "CONST DECL" : "LET DECL");
            for (i = 0; i < depth + 1; i++) printf("  ");
            printf("name=%s\n", node->data.declaration.name ? node->data.declaration.name : "<anon>");
            break;
        case TYPE_FUNCTION:
            printf("FUNCTION\n");
            break;
    }
}

static void free_xon_ast(DataNode* node) {
    if (!node) return;

    if (node->next) {
        free_xon_ast(node->next);
    }

    if (node->type == TYPE_EXPR && node->data.expr) {
        XonExpr* expr = node->data.expr;
        switch (expr->kind) {
            case XON_EXPR_IDENTIFIER:
                free(expr->u.identifier_name);
                break;
            case XON_EXPR_BINARY:
                free_xon_ast(expr->u.binary.left);
                free_xon_ast(expr->u.binary.right);
                break;
            case XON_EXPR_UNARY:
                free_xon_ast(expr->u.unary.operand);
                break;
            case XON_EXPR_CALL:
                free_xon_ast(expr->u.call.callee);
                free_xon_ast(expr->u.call.args);
                break;
            case XON_EXPR_MEMBER:
                free_xon_ast(expr->u.member.object);
                free(expr->u.member.member);
                break;
            case XON_EXPR_TERNARY:
            case XON_EXPR_IF:
                free_xon_ast(expr->u.ternary.cond);
                free_xon_ast(expr->u.ternary.then_expr);
                free_xon_ast(expr->u.ternary.else_expr);
                break;
            case XON_EXPR_FUNCTION:
                free_xon_ast(expr->u.function.params);
                free_xon_ast(expr->u.function.body);
                break;
            default:
                break;
        }
        free(expr);
    } else if (node->type == TYPE_FUNCTION) {
        RuntimeFunction* fn = (RuntimeFunction*)node->data.function_data;
        if (fn) {
            fn->ref_count--;
            if (fn->ref_count <= 0) {
                if (!fn->is_native) {
                    eval_scope_release(fn->impl.user.closure);
                    free_xon_ast(fn->impl.user.params);
                    free_xon_ast(fn->impl.user.body);
                }
                free(fn);
            }
        }
    }

    if (node->type == TYPE_STRING) {
        free(node->data.s_val);
    } else if (node->type == TYPE_OBJECT) {
        free_xon_ast(node->data.aggregate.key);
        free_xon_ast(node->data.aggregate.value);
    } else if (node->type == TYPE_LIST) {
        free_xon_ast(node->data.aggregate.value);
    } else if (node->type == TYPE_DECL) {
        free(node->data.declaration.name);
        free_xon_ast(node->data.declaration.init_expr);
    }

    free(node);
}
static DataNode* make_null_node(void) {
    return new_node(TYPE_NULL);
}

static DataNode* make_bool_node(int value) {
    DataNode* node = new_node(TYPE_BOOL);
    if (node) node->data.b_val = value ? 1 : 0;
    return node;
}

static DataNode* make_number_node(double value) {
    DataNode* node = new_node(TYPE_NUMBER);
    if (node) node->data.n_val = value;
    return node;
}

static DataNode* make_string_node(const char* value) {
    DataNode* node = new_node(TYPE_STRING);
    if (!node) return NULL;

    if (!value) {
        node->data.s_val = NULL;
        return node;
    }

    node->data.s_val = clone_c_string(value);
    if (!node->data.s_val) {
        free(node);
        return NULL;
    }
    return node;
}

static void eval_scope_retain(EvalScope* scope) {
    if (!scope) return;
    scope->ref_count++;
}

static EvalScope* eval_scope_new(EvalScope* parent) {
    EvalScope* scope = (EvalScope*)malloc(sizeof(EvalScope));
    if (!scope) return NULL;
    scope->parent = parent;
    scope->first = NULL;
    scope->ref_count = 1;
    if (parent) eval_scope_retain(parent);
    return scope;
}

static void eval_scope_release(EvalScope* scope) {
    EvalBinding* binding;
    EvalBinding* next;

    if (!scope) return;

    scope->ref_count--;
    if (scope->ref_count > 0) return;

    binding = scope->first;
    while (binding) {
        next = binding->next;
        free(binding->name);
        if (binding->init_expr) free_xon_ast(binding->init_expr);
        if (binding->value) free_xon_ast(binding->value);
        free(binding);
        binding = next;
    }

    if (scope->parent) {
        eval_scope_release(scope->parent);
    }
    free(scope);
}

static EvalBinding* eval_scope_find_binding(EvalScope* scope, const char* name) {
    EvalBinding* current;
    if (!scope || !name) return NULL;
    current = scope->first;
    while (current) {
        if (strcmp(current->name, name) == 0) return current;
        current = current->next;
    }
    return NULL;
}

static EvalBinding* eval_scope_lookup(EvalScope* scope, const char* name) {
    EvalScope* current;
    EvalBinding* binding;

    if (!name) return NULL;

    current = scope;
    while (current) {
        binding = eval_scope_find_binding(current, name);
        if (binding) return binding;
        current = current->parent;
    }

    return NULL;
}

static EvalBinding* eval_scope_declare(EvalScope* scope, const char* name, int is_const, DataNode* init_expr, EvalError* err) {
    EvalBinding* binding;
    char* copied_name;

    if (!scope || !name) {
        eval_set_error(err, "Invalid declaration binding");
        if (init_expr) free_xon_ast(init_expr);
        return NULL;
    }

    if (eval_scope_find_binding(scope, name)) {
        eval_set_error(err, "Duplicate declaration in same scope");
        if (init_expr) free_xon_ast(init_expr);
        return NULL;
    }

    copied_name = clone_c_string(name);
    if (!copied_name) {
        eval_set_error(err, "Out of memory during declaration");
        if (init_expr) free_xon_ast(init_expr);
        return NULL;
    }

    binding = (EvalBinding*)malloc(sizeof(EvalBinding));
    if (!binding) {
        free(copied_name);
        if (init_expr) free_xon_ast(init_expr);
        eval_set_error(err, "Out of memory during declaration");
        return NULL;
    }

    binding->name = copied_name;
    binding->is_const = is_const;
    binding->initialized = 0;
    binding->resolving = 0;
    binding->init_expr = init_expr;
    binding->value = NULL;
    binding->function = NULL;
    binding->next = scope->first;
    scope->first = binding;

    return binding;
}

static void eval_set_binding_value(EvalScope* scope, const char* name, DataNode* value, int is_const, EvalError* err) {
    EvalBinding* binding;

    if (!scope || !name || !value) {
        if (value) free_xon_ast(value);
        return;
    }

    binding = eval_scope_find_binding(scope, name);
    if (!binding) {
        binding = eval_scope_declare(scope, name, is_const, NULL, err);
    }
    if (!binding || !binding->name) {
        free_xon_ast(value);
        return;
    }
    if (binding->is_const && binding->initialized) {
        free_xon_ast(value);
        eval_set_error(err, "Cannot assign to constant");
        return;
    }

    if (binding->value) {
        free_xon_ast(binding->value);
    }
    binding->value = value;
    binding->initialized = 1;
    binding->resolving = 0;
}

static DataNode* clone_data_node(const DataNode* src);

static XonExpr* clone_expr(const XonExpr* expr) {
    DataNode* left;
    DataNode* right;
    DataNode* callee;
    DataNode* args;
    DataNode* obj;
    DataNode* body;
    DataNode* params;

    if (!expr) return NULL;

    switch (expr->kind) {
        case XON_EXPR_IDENTIFIER:
            return xon_expr_identifier(clone_c_string(expr->u.identifier_name), expr->line);
        case XON_EXPR_BINARY:
            left = clone_data_node(expr->u.binary.left);
            right = clone_data_node(expr->u.binary.right);
            if (!left || !right) {
                if (left) free_xon_ast(left);
                if (right) free_xon_ast(right);
                return NULL;
            }
            return xon_expr_binary(expr->u.binary.op, left, right, expr->line);
        case XON_EXPR_UNARY:
            left = clone_data_node(expr->u.unary.operand);
            if (!left) return NULL;
            return xon_expr_unary(expr->u.unary.op, left, expr->line);
        case XON_EXPR_CALL:
            callee = clone_data_node(expr->u.call.callee);
            args = clone_data_node(expr->u.call.args);
            if (!callee || (expr->u.call.args && !args)) {
                if (callee) free_xon_ast(callee);
                if (args) free_xon_ast(args);
                return NULL;
            }
            return xon_expr_call(callee, args, expr->line);
        case XON_EXPR_MEMBER:
            obj = clone_data_node(expr->u.member.object);
            if (!obj) return NULL;
            return xon_expr_member(obj, clone_c_string(expr->u.member.member), expr->line);
        case XON_EXPR_TERNARY:
        case XON_EXPR_IF:
            left = clone_data_node(expr->u.ternary.cond);
            right = clone_data_node(expr->u.ternary.then_expr);
            body = clone_data_node(expr->u.ternary.else_expr);
            if (!left || !right || !body) {
                if (left) free_xon_ast(left);
                if (right) free_xon_ast(right);
                if (body) free_xon_ast(body);
                return NULL;
            }
            return xon_expr_ternary(left, right, body, expr->line);
        case XON_EXPR_FUNCTION:
            params = clone_data_node(expr->u.function.params);
            body = clone_data_node(expr->u.function.body);
            if (!params && expr->u.function.params) {
                if (body) free_xon_ast(body);
                return NULL;
            }
            if (!body) return NULL;
            return xon_expr_function(params, body, expr->line);
        default:
            return NULL;
    }
}

static DataNode* clone_data_node(const DataNode* src) {
    DataNode* dst;
    DataNode* current;
    DataNode* cloned;
    DataNode* tail;

    if (!src) return make_null_node();

    dst = new_node(src->type);
    if (!dst) return NULL;

    switch (src->type) {
        case TYPE_STRING:
            dst->data.s_val = clone_c_string(src->data.s_val);
            if (src->data.s_val && !dst->data.s_val) {
                free(dst);
                return NULL;
            }
            return dst;
        case TYPE_NUMBER:
            dst->data.n_val = src->data.n_val;
            return dst;
        case TYPE_BOOL:
            dst->data.b_val = src->data.b_val;
            return dst;
        case TYPE_NULL:
            return dst;
        case TYPE_EXPR:
            dst->data.expr = clone_expr(src->data.expr);
            if (!dst->data.expr) {
                free(dst);
                return NULL;
            }
            return dst;
        case TYPE_DECL:
            dst->data.declaration.is_const = src->data.declaration.is_const;
            dst->data.declaration.name = clone_c_string(src->data.declaration.name);
            if (src->data.declaration.name && !dst->data.declaration.name) {
                free(dst);
                return NULL;
            }
            if (src->data.declaration.init_expr) {
                dst->data.declaration.init_expr = clone_data_node(src->data.declaration.init_expr);
                if (!dst->data.declaration.init_expr) {
                    free(dst->data.declaration.name);
                    free(dst);
                    return NULL;
                }
            }
            return dst;
        case TYPE_FUNCTION: {
            RuntimeFunction* fn = (RuntimeFunction*)src->data.function_data;
            if (!fn) {
                free(dst);
                return NULL;
            }
            fn->ref_count++;
            dst->data.function_data = fn;
            return dst;
        }
        case TYPE_OBJECT:
            if (src->data.aggregate.key) {
                dst->data.aggregate.key = clone_data_node(src->data.aggregate.key);
                if (!dst->data.aggregate.key) {
                    free(dst);
                    return NULL;
                }
                if (src->data.aggregate.value) {
                    dst->data.aggregate.value = clone_data_node(src->data.aggregate.value);
                    if (!dst->data.aggregate.value) {
                        free_xon_ast(dst);
                        return NULL;
                    }
                }
                return dst;
            }
            /* fallthrough for object containers (pairs list) */
        case TYPE_LIST:
            current = src->data.aggregate.value;
            dst->data.aggregate.value = NULL;
            tail = NULL;
            while (current) {
                cloned = clone_data_node(current);
                if (!cloned) {
                    free_xon_ast(dst);
                    return NULL;
                }
                if (!tail) {
                    dst->data.aggregate.value = cloned;
                    tail = cloned;
                } else {
                    tail->next = cloned;
                    tail = cloned;
                }
                current = current->next;
            }
            return dst;
        default:
            free(dst);
            return NULL;
    }
}

static int is_number_type(const DataNode* value) {
    return value && value->type == TYPE_NUMBER;
}

static int is_string_type(const DataNode* value) {
    return value && value->type == TYPE_STRING;
}

static int is_truthy(const DataNode* value) {
    if (!value || value->type == TYPE_NULL) return 0;
    if (value->type == TYPE_BOOL) return value->data.b_val != 0;
    if (value->type == TYPE_NUMBER) return value->data.n_val != 0.0;
    if (value->type == TYPE_STRING) return value->data.s_val && value->data.s_val[0] != '\0';
    return 1;
}

static int values_equal(const DataNode* left, const DataNode* right) {
    if (!left || !right) return left == right;
    if (left->type != right->type) return 0;

    switch (left->type) {
        case TYPE_NULL:
            return 1;
        case TYPE_BOOL:
            return left->data.b_val == right->data.b_val;
        case TYPE_NUMBER:
            return left->data.n_val == right->data.n_val;
        case TYPE_STRING:
            return (left->data.s_val && right->data.s_val && strcmp(left->data.s_val, right->data.s_val) == 0);
        default:
            return left == right;
    }
}

static char* clone_c_string(const char* src) {
    char* out;
    size_t len;

    if (!src) return NULL;
    len = strlen(src);
    out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, src, len + 1);
    return out;
}

static DataNode* runtime_to_string_node(const DataNode* node, EvalError* err) {
    char* rendered = NULL;
    char buffer[128];

    if (!node) return make_string_node("null");

    switch (node->type) {
        case TYPE_STRING:
            return make_string_node(node->data.s_val ? node->data.s_val : "");
        case TYPE_NUMBER:
            snprintf(buffer, sizeof(buffer), "%.17g", node->data.n_val);
            return make_string_node(buffer);
        case TYPE_BOOL:
            return make_string_node(node->data.b_val ? "true" : "false");
        case TYPE_NULL:
            return make_string_node("null");
        default:
            rendered = xon_to_json(node, 0);
            if (!rendered) {
                rendered = xon_to_xon(node, 0);
            }
            if (!rendered) {
                eval_set_error(err, "Failed to stringify value");
                return NULL;
            }
            {
                DataNode* out = make_string_node(rendered);
                free(rendered);
                return out;
            }
    }
}

static DataNode* builtin_abs(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    if (argc != 1 || !is_number_type(argv[0])) {
        eval_set_error(err, "abs() expects one number");
        return NULL;
    }
    return make_number_node(fabs(argv[0]->data.n_val));
}

static DataNode* builtin_len(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    if (argc != 1 || !argv[0]) {
        eval_set_error(err, "len() expects one argument");
        return NULL;
    }

    switch (argv[0]->type) {
        case TYPE_STRING: {
            return make_number_node((double)(argv[0]->data.s_val ? strlen(argv[0]->data.s_val) : 0));
        }
        case TYPE_LIST:
        case TYPE_OBJECT:
            return make_number_node((double)eval_list_size(argv[0]));
        default:
            eval_set_error(err, "len() expects string, list or object");
            return NULL;
    }
}

static DataNode* builtin_max(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    double best;
    size_t i;

    if (argc < 1) {
        eval_set_error(err, "max() expects at least one number");
        return NULL;
    }

    if (!is_number_type(argv[0])) {
        eval_set_error(err, "max() expects numeric arguments");
        return NULL;
    }
    best = argv[0]->data.n_val;

    for (i = 1; i < argc; i++) {
        if (!is_number_type(argv[i])) {
            eval_set_error(err, "max() expects numeric arguments");
            return NULL;
        }
        if (argv[i]->data.n_val > best) best = argv[i]->data.n_val;
    }

    return make_number_node(best);
}

static DataNode* builtin_min(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    double best;
    size_t i;

    if (argc < 1) {
        eval_set_error(err, "min() expects at least one number");
        return NULL;
    }

    if (!is_number_type(argv[0])) {
        eval_set_error(err, "min() expects numeric arguments");
        return NULL;
    }
    best = argv[0]->data.n_val;

    for (i = 1; i < argc; i++) {
        if (!is_number_type(argv[i])) {
            eval_set_error(err, "min() expects numeric arguments");
            return NULL;
        }
        if (argv[i]->data.n_val < best) best = argv[i]->data.n_val;
    }

    return make_number_node(best);
}

static DataNode* builtin_str(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    if (argc != 1) {
        eval_set_error(err, "str() expects one argument");
        return NULL;
    }
    return runtime_to_string_node(argv[0], err);
}

static DataNode* builtin_upper(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    char* input;
    char* cursor;
    DataNode* result;

    if (argc != 1 || !is_string_type(argv[0])) {
        eval_set_error(err, "upper() expects one string");
        return NULL;
    }

    input = clone_c_string(argv[0]->data.s_val ? argv[0]->data.s_val : "");
    if (!input) {
        eval_set_error(err, "Uppercase failed due to memory error");
        return NULL;
    }

    for (cursor = input; *cursor; cursor++) {
        *cursor = (char)toupper((unsigned char)*cursor);
    }

    result = make_string_node(input);
    free(input);
    return result;
}

static DataNode* builtin_lower(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    char* input;
    char* cursor;
    DataNode* result;

    if (argc != 1 || !is_string_type(argv[0])) {
        eval_set_error(err, "lower() expects one string");
        return NULL;
    }

    input = clone_c_string(argv[0]->data.s_val ? argv[0]->data.s_val : "");
    if (!input) {
        eval_set_error(err, "Lowercase failed due to memory error");
        return NULL;
    }

    for (cursor = input; *cursor; cursor++) {
        *cursor = (char)tolower((unsigned char)*cursor);
    }

    result = make_string_node(input);
    free(input);
    return result;
}

static DataNode* builtin_keys(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    DataNode* result;
    DataNode* pair = NULL;
    DataNode* list_tail = NULL;

    if (argc != 1 || !argv[0] || argv[0]->type != TYPE_OBJECT) {
        eval_set_error(err, "keys() expects one object");
        return NULL;
    }

    result = new_node(TYPE_LIST);
    if (!result) {
        eval_set_error(err, "Out of memory creating list");
        return NULL;
    }

    pair = argv[0]->data.aggregate.value;
    while (pair) {
        DataNode* value;
        if (!pair->data.aggregate.key || !pair->data.aggregate.key->data.s_val) {
            eval_set_error(err, "Object key missing while computing keys()");
            free_xon_ast(result);
            return NULL;
        }
        value = make_string_node(pair->data.aggregate.key->data.s_val);
        if (!value) {
            free_xon_ast(result);
            eval_set_error(err, "Out of memory for keys()");
            return NULL;
        }
        if (!result->data.aggregate.value) {
            result->data.aggregate.value = value;
            list_tail = value;
        } else {
            list_tail->next = value;
            list_tail = value;
        }
        pair = pair->next;
    }

    return result;
}

static DataNode* builtin_has(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    const DataNode* obj;
    const DataNode* key;
    const DataNode* current;

    if (argc != 2) {
        eval_set_error(err, "has() expects an object and a string key");
        return NULL;
    }

    obj = argv[0];
    key = argv[1];
    if (!obj || obj->type != TYPE_OBJECT || !is_string_type(key)) {
        eval_set_error(err, "has() expects an object and a string key");
        return NULL;
    }

    current = obj->data.aggregate.value;
    while (current) {
        if (current->type == TYPE_OBJECT &&
            current->data.aggregate.key &&
            current->data.aggregate.key->data.s_val &&
            key->data.s_val &&
            strcmp(current->data.aggregate.key->data.s_val, key->data.s_val) == 0) {
            return make_bool_node(1);
        }
        current = current->next;
    }

    return make_bool_node(0);
}

static DataNode* builtin_env(size_t argc, const DataNode* const* argv, void* userdata) {
    EvalError* err = (EvalError*)userdata;
    const char* value;

    if (argc != 1 || !is_string_type(argv[0])) {
        eval_set_error(err, "env() expects one string key");
        return NULL;
    }

    value = getenv(argv[0]->data.s_val ? argv[0]->data.s_val : "");
    if (!value) return make_null_node();
    return make_string_node(value);
}

static const BuiltinSpec BUILTIN_SPECS[] = {
    {"abs", builtin_abs, 0, 1, 1},
    {"len", builtin_len, 0, 1, 1},
    {"min", builtin_min, 1, 1, 0},
    {"max", builtin_max, 1, 1, 0},
    {"str", builtin_str, 0, 1, 1},
    {"upper", builtin_upper, 0, 1, 1},
    {"lower", builtin_lower, 0, 1, 1},
    {"keys", builtin_keys, 0, 1, 1},
    {"has", builtin_has, 0, 2, 2},
    {"env", builtin_env, 0, 1, 1}
};

static size_t eval_list_size(const DataNode* list) {
    size_t count = 0;
    const DataNode* item = list;

    if (!list) return 0;

    if (list->type == TYPE_LIST) {
        item = list->data.aggregate.value;
    }

    while (item) {
        count++;
        item = item->next;
    }
    return count;
}

static int eval_is_identifier(const char* key) {
    size_t i;
    if (!key || !key[0]) return 0;

    if (!(isalpha((unsigned char)key[0]) || key[0] == '_' || key[0] == '$')) return 0;
    for (i = 1; key[i]; i++) {
        if (!(isalnum((unsigned char)key[i]) || key[i] == '_' || key[i] == '$')) return 0;
    }
    return 1;
}

static DataNode* eval_lookup_identifier(const char* name, EvalScope* scope, EvalError* err) {
    EvalBinding* binding;
    const char* env_value;

    if (!name) {
        eval_set_error(err, "Invalid identifier");
        return NULL;
    }

    binding = eval_scope_lookup(scope, name);
    if (binding) {
        if (binding->value && binding->initialized) {
            return clone_data_node(binding->value);
        }
        if (binding->resolving) {
            eval_set_error(err, "Circular variable reference");
            return NULL;
        }
        if (!binding->init_expr) {
            eval_set_error(err, "Uninitialized identifier");
            return NULL;
        }

        binding->resolving = 1;
        binding->value = xon_eval_node(binding->init_expr, scope, err);
        binding->resolving = 0;
        if (binding->value && !err->active) {
            binding->initialized = 1;
            free_xon_ast(binding->init_expr);
            binding->init_expr = NULL;
            return clone_data_node(binding->value);
        }
        return NULL;
    }

    if (scope && eval_is_identifier(name) == 0) {
        eval_set_error(err, "Invalid identifier syntax");
        return NULL;
    }

    env_value = getenv(name);
    if (env_value) {
        return make_string_node(env_value);
    }

    eval_set_error(err, "Unknown identifier");
    return NULL;
}

static DataNode* eval_expr_node(const XonExpr* expr, EvalScope* scope, EvalError* err) {
    DataNode* left;
    DataNode* right;
    DataNode* result;
    double divisor;
    DataNode* call_args;
    DataNode** evaluated_args = NULL;
    size_t argc = 0;
    RuntimeFunction* fn;

    if (!expr) return make_null_node();

    switch (expr->kind) {
        case XON_EXPR_IDENTIFIER:
            return eval_lookup_identifier(expr->u.identifier_name, scope, err);
        case XON_EXPR_BINARY:
            if (expr->u.binary.op == XON_EXPR_OP_OR || expr->u.binary.op == XON_EXPR_OP_AND ||
                expr->u.binary.op == XON_EXPR_OP_NULLISH) {
                left = xon_eval_node(expr->u.binary.left, scope, err);
                if (err->active) return NULL;

                if (expr->u.binary.op == XON_EXPR_OP_OR) {
                    if (is_truthy(left)) return left;
                    free_xon_ast(left);
                    return xon_eval_node(expr->u.binary.right, scope, err);
                }

                if (expr->u.binary.op == XON_EXPR_OP_AND) {
                    if (!is_truthy(left)) return left;
                    free_xon_ast(left);
                    return xon_eval_node(expr->u.binary.right, scope, err);
                }

                if (left->type == TYPE_NULL) {
                    free_xon_ast(left);
                    return xon_eval_node(expr->u.binary.right, scope, err);
                }
                return left;
            }

            left = xon_eval_node(expr->u.binary.left, scope, err);
            if (err->active) return NULL;
            right = xon_eval_node(expr->u.binary.right, scope, err);
            if (err->active) {
                free_xon_ast(left);
                return NULL;
            }

            switch (expr->u.binary.op) {
                case XON_EXPR_OP_EQ:
                    result = values_equal(left, right) ? make_bool_node(1) : make_bool_node(0);
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                case XON_EXPR_OP_NEQ:
                    result = values_equal(left, right) ? make_bool_node(0) : make_bool_node(1);
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                case XON_EXPR_OP_ADD:
                    if (is_number_type(left) && is_number_type(right)) {
                        result = make_number_node(left->data.n_val + right->data.n_val);
                    } else if (is_string_type(left) && is_string_type(right)) {
                        size_t len = (left->data.s_val ? strlen(left->data.s_val) : 0) +
                                     (right->data.s_val ? strlen(right->data.s_val) : 0);
                        char* out = (char*)malloc(len + 1);
                        if (!out) {
                            eval_set_error(err, "Out of memory during string concat");
                            free_xon_ast(left);
                            free_xon_ast(right);
                            return NULL;
                        }
                        out[0] = '\0';
                        if (left->data.s_val) strcat(out, left->data.s_val);
                        if (right->data.s_val) strcat(out, right->data.s_val);
                        result = make_string_node(out);
                        free(out);
                    } else {
                        eval_set_error(err, "Invalid operands for '+'");
                        result = NULL;
                    }
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                case XON_EXPR_OP_SUB:
                    if (!is_number_type(left) || !is_number_type(right)) {
                        eval_set_error(err, "Invalid operands for '-'");
                        result = NULL;
                    } else {
                        result = make_number_node(left->data.n_val - right->data.n_val);
                    }
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                case XON_EXPR_OP_MUL:
                    if (!is_number_type(left) || !is_number_type(right)) {
                        eval_set_error(err, "Invalid operands for '*'");
                        result = NULL;
                    } else {
                        result = make_number_node(left->data.n_val * right->data.n_val);
                    }
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                case XON_EXPR_OP_DIV:
                    if (!is_number_type(left) || !is_number_type(right)) {
                        eval_set_error(err, "Invalid operands for '/'");
                        result = NULL;
                    } else {
                        divisor = right->data.n_val;
                        if (divisor == 0.0) {
                            eval_set_error(err, "Division by zero");
                            result = NULL;
                        } else {
                            result = make_number_node(left->data.n_val / divisor);
                        }
                    }
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                case XON_EXPR_OP_MOD:
                    if (!is_number_type(left) || !is_number_type(right)) {
                        eval_set_error(err, "Invalid operands for '%'");
                        result = NULL;
                    } else {
                        divisor = right->data.n_val;
                        if (divisor == 0.0) {
                            eval_set_error(err, "Modulo by zero");
                            result = NULL;
                        } else {
                            result = make_number_node(fmod(left->data.n_val, divisor));
                        }
                    }
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                case XON_EXPR_OP_LT:
                case XON_EXPR_OP_LTE:
                case XON_EXPR_OP_GT:
                case XON_EXPR_OP_GTE:
                    if (!is_number_type(left) || !is_number_type(right)) {
                        eval_set_error(err, "Invalid operands for comparison");
                        result = NULL;
                    } else if (expr->u.binary.op == XON_EXPR_OP_LT) {
                        result = make_bool_node(left->data.n_val < right->data.n_val);
                    } else if (expr->u.binary.op == XON_EXPR_OP_LTE) {
                        result = make_bool_node(left->data.n_val <= right->data.n_val);
                    } else if (expr->u.binary.op == XON_EXPR_OP_GT) {
                        result = make_bool_node(left->data.n_val > right->data.n_val);
                    } else {
                        result = make_bool_node(left->data.n_val >= right->data.n_val);
                    }
                    free_xon_ast(left);
                    free_xon_ast(right);
                    return result;
                default:
                    free_xon_ast(left);
                    free_xon_ast(right);
                    eval_set_error(err, "Unsupported binary operator");
                    return NULL;
            }
        case XON_EXPR_UNARY: {
            DataNode* operand = xon_eval_node(expr->u.unary.operand, scope, err);
            if (!operand) return NULL;
            switch (expr->u.unary.op) {
                case XON_EXPR_OP_NOT:
                    result = make_bool_node(!is_truthy(operand));
                    break;
                case XON_EXPR_OP_NEG:
                    if (!is_number_type(operand)) {
                        eval_set_error(err, "Invalid operand for unary '-'");
                        result = NULL;
                    } else {
                        result = make_number_node(-operand->data.n_val);
                    }
                    break;
                case XON_EXPR_OP_UNARY_PLUS:
                    if (!is_number_type(operand)) {
                        eval_set_error(err, "Invalid operand for unary '+'");
                        result = NULL;
                    } else {
                        result = make_number_node(+operand->data.n_val);
                    }
                    break;
                default:
                    result = NULL;
                    eval_set_error(err, "Unsupported unary operator");
                    break;
            }
            free_xon_ast(operand);
            return result;
        }
        case XON_EXPR_MEMBER: {
            DataNode* object = xon_eval_node(expr->u.member.object, scope, err);
            if (!object || object->type != TYPE_OBJECT) {
                if (object) free_xon_ast(object);
                eval_set_error(err, "Member access requires object");
                return NULL;
            }
            result = xon_get_key_internal(object, expr->u.member.member);
            if (!result) {
                free_xon_ast(object);
                eval_set_error(err, "Unknown object member");
                return NULL;
            }
            {
                DataNode* out = clone_data_node(result);
                free_xon_ast(object);
                return out;
            }
        }
        case XON_EXPR_TERNARY: {
            left = xon_eval_node(expr->u.ternary.cond, scope, err);
            if (!left) return NULL;
            if (is_truthy(left)) {
                free_xon_ast(left);
                return xon_eval_node(expr->u.ternary.then_expr, scope, err);
            }
            free_xon_ast(left);
            return xon_eval_node(expr->u.ternary.else_expr, scope, err);
        }
        case XON_EXPR_IF:
            return eval_expr_node(&(XonExpr){XON_EXPR_TERNARY, 0, { .ternary = {
                expr->u.ternary.cond,
                expr->u.ternary.then_expr,
                expr->u.ternary.else_expr
            }}}, scope, err);
        case XON_EXPR_CALL: {
            DataNode* callee = xon_eval_node(expr->u.call.callee, scope, err);
            if (!callee) return NULL;

            if (callee->type != TYPE_FUNCTION) {
                free_xon_ast(callee);
                eval_set_error(err, "Attempted call on non-function");
                return NULL;
            }

            fn = (RuntimeFunction*)callee->data.function_data;
            if (!fn) {
                free_xon_ast(callee);
                eval_set_error(err, "Invalid function value");
                return NULL;
            }

            call_args = expr->u.call.args;
            while (call_args) {
                DataNode* arg = xon_eval_node(call_args, scope, err);
                if (err->active) {
                    free_xon_ast(arg);
                    break;
                }
                {
                    DataNode** resized = (DataNode**)realloc(evaluated_args, sizeof(DataNode*) * (argc + 1));
                    if (!resized) {
                        eval_set_error(err, "Out of memory evaluating arguments");
                        free_xon_ast(arg);
                        break;
                    }
                    evaluated_args = resized;
                    evaluated_args[argc++] = arg;
                }
                call_args = call_args->next;
            }

            if (err->active) {
                size_t i;
                for (i = 0; i < argc; i++) free_xon_ast(evaluated_args[i]);
                free(evaluated_args);
                free_xon_ast(callee);
                return NULL;
            }

            result = eval_call(fn, argc, evaluated_args, err);
            {
                size_t i;
                for (i = 0; i < argc; i++) free_xon_ast(evaluated_args[i]);
                free(evaluated_args);
            }
            free_xon_ast(callee);
            return result;
        }
        case XON_EXPR_FUNCTION: {
            RuntimeFunction* fn_data;
            DataNode* function_node;
            size_t arity = 0;
            const DataNode* p = expr->u.function.params;
            const DataNode* current = NULL;

            if (p && p->type == TYPE_LIST) {
                current = p->data.aggregate.value;
            } else {
                current = p;
            }

            if (current) {
                while (current) {
                    if (current->type != TYPE_STRING || !current->data.s_val) {
                        eval_set_error(err, "Function parameter must be identifier");
                        return NULL;
                    }
                    if (!eval_is_identifier(current->data.s_val)) {
                        eval_set_error(err, "Invalid function parameter identifier");
                        return NULL;
                    }
                    arity++;
                    current = current->next;
                }
            }

            fn_data = (RuntimeFunction*)malloc(sizeof(RuntimeFunction));
            if (!fn_data) {
                eval_set_error(err, "Out of memory creating function");
                return NULL;
            }

            fn_data->is_native = 0;
            fn_data->arity_min = arity;
            fn_data->arity_max = arity;
            fn_data->impl.user.params = clone_data_node(expr->u.function.params);
            fn_data->impl.user.body = clone_data_node(expr->u.function.body);
            fn_data->userdata = NULL;
            fn_data->ref_count = 1;
            if (!fn_data->impl.user.params && expr->u.function.params) {
                free(fn_data);
                eval_set_error(err, "Out of memory cloning function body");
                return NULL;
            }
            if (!fn_data->impl.user.body) {
                free_xon_ast(fn_data->impl.user.params);
                free(fn_data);
                eval_set_error(err, "Out of memory cloning function body");
                return NULL;
            }

            fn_data->impl.user.closure = eval_scope_new(scope);
            if (!fn_data->impl.user.closure) {
                free_xon_ast(fn_data->impl.user.params);
                free_xon_ast(fn_data->impl.user.body);
                free(fn_data);
                eval_set_error(err, "Out of memory creating function scope");
                return NULL;
            }

            function_node = new_node(TYPE_FUNCTION);
            if (!function_node) {
                eval_scope_release(fn_data->impl.user.closure);
                free_xon_ast(fn_data->impl.user.params);
                free_xon_ast(fn_data->impl.user.body);
                free(fn_data);
                eval_set_error(err, "Out of memory creating function node");
                return NULL;
            }
            function_node->data.function_data = fn_data;
            return function_node;
        }
    }

    eval_set_error(err, "Unknown expression node");
    return NULL;
}

static DataNode* eval_object_node(const DataNode* node, EvalScope* scope, EvalError* err) {
    const DataNode* pair = NULL;
    DataNode* out = NULL;
    DataNode* tail = NULL;

    if (!node || node->type != TYPE_OBJECT) {
        eval_set_error(err, "Expected object value");
        return NULL;
    }

    pair = node->data.aggregate.value;
    while (pair) {
        if (pair->type == TYPE_DECL) {
            if (!pair->data.declaration.name) {
                eval_set_error(err, "Invalid declaration name");
                free_xon_ast(out);
                return NULL;
            }
            if (!eval_scope_declare(scope, pair->data.declaration.name, pair->data.declaration.is_const,
                                   clone_data_node(pair->data.declaration.init_expr), err)) {
                free_xon_ast(out);
                return NULL;
            }
        }
        pair = pair->next;
    }

    pair = node->data.aggregate.value;
    while (pair) {
        if (pair->type == TYPE_DECL) {
            EvalBinding* binding = eval_scope_find_binding(scope, pair->data.declaration.name);
            if (!binding) {
                eval_set_error(err, "Internal declaration lookup failed");
                free_xon_ast(out);
                return NULL;
            }

            if (!binding->initialized && !binding->resolving) {
                DataNode* init = pair->data.declaration.init_expr;
                if (!init) {
                    binding->initialized = 1;
                } else {
                    binding->resolving = 1;
                    binding->value = xon_eval_node(init, scope, err);
                    binding->resolving = 0;
                    if (err->active) {
                        free_xon_ast(out);
                        return NULL;
                    }
                    free_xon_ast(binding->init_expr);
                    binding->init_expr = NULL;
                    binding->initialized = 1;
                }
            }
            pair = pair->next;
            continue;
        }

        if (pair->type != TYPE_OBJECT) {
            eval_set_error(err, "Expected object property");
            free_xon_ast(out);
            return NULL;
        }

        {
            DataNode* out_pair = new_node(TYPE_OBJECT);
            if (!out_pair) {
                free_xon_ast(out);
                eval_set_error(err, "Out of memory building object result");
                return NULL;
            }
            out_pair->data.aggregate.key = clone_data_node(pair->data.aggregate.key);
            if (!out_pair->data.aggregate.key) {
                free_xon_ast(out_pair);
                free_xon_ast(out);
                eval_set_error(err, "Out of memory building object key");
                return NULL;
            }
            out_pair->data.aggregate.value = xon_eval_node(pair->data.aggregate.value, scope, err);
            if (!out_pair->data.aggregate.value) {
                free_xon_ast(out_pair);
                free_xon_ast(out);
                return NULL;
            }

            if (!out) {
                out = new_node(TYPE_OBJECT);
                if (!out) {
                    free_xon_ast(out_pair);
                    eval_set_error(err, "Out of memory building object");
                    return NULL;
                }
                out->data.aggregate.value = out_pair;
                tail = out_pair;
            } else {
                tail->next = out_pair;
                tail = out_pair;
            }
        }
        pair = pair->next;
    }

    if (!out) return new_node(TYPE_OBJECT);
    return out;
}

static DataNode* eval_list_node(const DataNode* node, EvalScope* scope, EvalError* err) {
    const DataNode* item = NULL;
    DataNode* out = new_node(TYPE_LIST);
    DataNode* tail = NULL;
    DataNode* value = NULL;

    if (!out) {
        eval_set_error(err, "Out of memory building list");
        return NULL;
    }

    item = node->data.aggregate.value;
    while (item) {
        value = xon_eval_node(item, scope, err);
        if (!value) {
            free_xon_ast(out);
            return NULL;
        }

        if (!out->data.aggregate.value) {
            out->data.aggregate.value = value;
            tail = value;
        } else {
            tail->next = value;
            tail = value;
        }
        item = item->next;
    }

    return out;
}

static DataNode* eval_call(RuntimeFunction* fn, size_t argc, DataNode* const* argv, EvalError* err) {
    size_t i;
    if (!fn) {
        eval_set_error(err, "Invalid callable value");
        return NULL;
    }

    if (argc < fn->arity_min) {
        eval_set_error(err, "Function call received too few arguments");
        return NULL;
    }
    if (fn->arity_max != (size_t)-1 && argc > fn->arity_max) {
        eval_set_error(err, "Function call received too many arguments");
        return NULL;
    }

    if (fn->is_native) {
        return fn->impl.native(argc, (const DataNode* const*)argv, err);
    }

    {
        EvalScope* fn_scope = eval_scope_new(fn->impl.user.closure);
        const DataNode* params = fn->impl.user.params;
        const DataNode* param = params ? (params->type == TYPE_LIST ? params->data.aggregate.value : params) : NULL;
        DataNode* result = NULL;

        if (!fn_scope) {
            eval_set_error(err, "Out of memory creating function scope");
            return NULL;
        }

        for (i = 0; i < argc; i++) {
            const char* param_name = NULL;
            if (param) {
                param_name = param->data.s_val;
            }
            if (!param_name) {
                eval_set_error(err, "Too many arguments for function");
                eval_scope_release(fn_scope);
                return NULL;
            }
            eval_set_binding_value(fn_scope, param_name, clone_data_node(argv[i]), 0, err);
            if (err->active) {
                eval_scope_release(fn_scope);
                return NULL;
            }
            param = param->next;
        }

        if (param) {
            eval_set_error(err, "Missing required function arguments");
            eval_scope_release(fn_scope);
            return NULL;
        }

        result = xon_eval_node(fn->impl.user.body, fn_scope, err);
        eval_scope_release(fn_scope);
        if (err->active) {
            if (result) free_xon_ast(result);
            return NULL;
        }
        return result;
    }
}

static int eval_register_builtin(EvalScope* scope, const BuiltinSpec* spec, EvalError* err) {
    RuntimeFunction* fn;
    DataNode* node;
    EvalBinding* binding;

    if (!scope || !spec || !spec->name) return 0;

    fn = (RuntimeFunction*)malloc(sizeof(RuntimeFunction));
    if (!fn) {
        eval_set_error(err, "Out of memory creating builtin function");
        return 0;
    }

    fn->is_native = 1;
    fn->arity_min = spec->arity_min;
    fn->arity_max = spec->is_variadic ? (size_t)-1 : spec->arity_max;
    fn->impl.native = spec->impl;
    fn->ref_count = 1;
    fn->userdata = NULL;

    node = new_node(TYPE_FUNCTION);
    if (!node) {
        free(fn);
        eval_set_error(err, "Out of memory creating builtin function value");
        return 0;
    }
    node->data.function_data = fn;

    binding = eval_scope_declare(scope, spec->name, 1, NULL, err);
    if (!binding) {
        free_xon_ast(node);
        return 0;
    }
    binding->value = node;
    binding->initialized = 1;
    return 1;
}

static DataNode* xon_eval_node(const DataNode* node, EvalScope* scope, EvalError* err) {
    if (!node) return NULL;
    if (err && err->active) return NULL;

    switch (node->type) {
        case TYPE_OBJECT:
            return eval_object_node(node, scope, err);
        case TYPE_LIST:
            return eval_list_node(node, scope, err);
        case TYPE_EXPR:
            return eval_expr_node(node->data.expr, scope, err);
        case TYPE_DECL:
            if (node->data.declaration.init_expr) {
                return xon_eval_node(node->data.declaration.init_expr, scope, err);
            }
            eval_set_error(err, "Invalid declaration without initializer");
            return NULL;
        case TYPE_STRING:
            return make_string_node(node->data.s_val);
        case TYPE_NUMBER:
            return make_number_node(node->data.n_val);
        case TYPE_BOOL:
            return make_bool_node(node->data.b_val);
        case TYPE_NULL:
            return make_null_node();
        case TYPE_FUNCTION:
            return clone_data_node(node);
        default:
            eval_set_error(err, "Unsupported node type for evaluation");
            return NULL;
    }
}

static void on_syntax_error(int line, const char* token, void* user_data) {
    (void)user_data;
    fprintf(stderr, "Syntax Error at line %d near token '%s'\n", line, token ? token : "unknown");
    xon_log_error("parser", "Syntax Error at line %d near token '%s'", line, token ? token : "unknown");
}

static EvalScope* eval_create_global_scope(EvalError* err) {
    EvalScope* scope = eval_scope_new(NULL);
    size_t i;

    if (!scope) {
        eval_set_error(err, "Out of memory creating evaluation scope");
        return NULL;
    }

    for (i = 0; i < (sizeof(BUILTIN_SPECS) / sizeof(BUILTIN_SPECS[0])); i++) {
        if (!eval_register_builtin(scope, &BUILTIN_SPECS[i], err)) {
            eval_scope_release(scope);
            return NULL;
        }
    }

    return scope;
}

XonValue* xon_eval(const XonValue* value) {
    DataNode* output;
    EvalScope* scope;
    EvalError err = {0};

    if (!value) return NULL;

    scope = eval_create_global_scope(&err);
    if (!scope) {
        if (err.active) {
            fprintf(stderr, "Xon Eval Error: %s\n", err.message);
            xon_log_error("eval", "Xon evaluation initialization failed: %s", err.message);
        }
        return NULL;
    }

    output = xon_eval_node((const DataNode*)value, scope, &err);
    eval_scope_release(scope);

    if (err.active) {
        if (output) free_xon_ast(output);
        fprintf(stderr, "Xon Eval Error: %s\n", err.message);
        xon_log_error("eval", "Xon evaluation failed: %s", err.message);
        return NULL;
    }

    xon_log_info("eval", "Evaluation completed");
    return output;
}

static DataNode* parse_stream(FILE* stream) {
    void* parser;
    ParserState state;
    DataNode* root = NULL;
    XonTokenData token_data;
    char* err_msg = NULL;
    int token_id;
    int current_line = 1;

    parser = xonParserAlloc(malloc);
    if (!parser) return NULL;
    xon_logger_init("xon");
    state.result = &root;
    state.had_error = 0;
    state.on_syntax_error = on_syntax_error;
    state.user_data = NULL;

    while ((token_id = xon_get_token(stream, &token_data, &err_msg, &current_line)) != 0) {
        Token parser_token;
        memset(&parser_token, 0, sizeof(parser_token));
        parser_token.s_val = token_data.sVal;
        parser_token.n_val = token_data.nVal;
        parser_token.line = current_line;

        if (token_id == -1) {
            if (err_msg) {
                fprintf(stderr, "Lexer Error at line %d: %s\n", current_line, err_msg);
                xon_log_error("lexer", "Lexer Error at line %d: %s", current_line, err_msg);
                free(err_msg);
                err_msg = NULL;
            }
            root = NULL;
            break;
        }

        xonParser(parser, token_id, parser_token, &state);
    }

    {
        Token end_token;
        memset(&end_token, 0, sizeof(end_token));
        end_token.line = current_line;
        xonParser(parser, 0, end_token, &state);
    }

    xonParserFree(parser, free);
    if (state.had_error) {
        if (root) free_xon_ast(root);
        root = NULL;
        xon_log_error("parser", "Parsing failed due to syntax errors");
    } else {
        xon_log_info("parser", "Parsing completed successfully");
    }
    return root;
}

static int sb_init(StringBuilder* sb) {
    sb->cap = 256;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (!sb->data) return 0;
    sb->data[0] = '\0';
    return 1;
}

static int sb_reserve(StringBuilder* sb, size_t add_len) {
    size_t required = sb->len + add_len + 1;
    if (required <= sb->cap) return 1;

    while (sb->cap < required) {
        sb->cap *= 2;
    }
    sb->data = (char*)realloc(sb->data, sb->cap);
    return sb->data != NULL;
}

static int sb_append_char(StringBuilder* sb, char c) {
    if (!sb_reserve(sb, 1)) return 0;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 1;
}

static int sb_append_str(StringBuilder* sb, const char* str) {
    size_t len;
    if (!str) return 1;
    len = strlen(str);
    if (!sb_reserve(sb, len)) return 0;
    memcpy(sb->data + sb->len, str, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return 1;
}

static int sb_append_indent(StringBuilder* sb, int depth) {
    int i;
    for (i = 0; i < depth; i++) {
        if (!sb_append_str(sb, "    ")) return 0;
    }
    return 1;
}

static int sb_append_escaped_string(StringBuilder* sb, const char* str) {
    const unsigned char* p = (const unsigned char*)str;
    if (!sb_append_char(sb, '"')) return 0;
    while (*p) {
        switch (*p) {
            case '\\': if (!sb_append_str(sb, "\\\\")) return 0; break;
            case '"': if (!sb_append_str(sb, "\\\"")) return 0; break;
            case '\n': if (!sb_append_str(sb, "\\n")) return 0; break;
            case '\r': if (!sb_append_str(sb, "\\r")) return 0; break;
            case '\t': if (!sb_append_str(sb, "\\t")) return 0; break;
            default:
                if (*p < 0x20) {
                    char escaped[7];
                    snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                    if (!sb_append_str(sb, escaped)) return 0;
                } else {
                    if (!sb_append_char(sb, (char)*p)) return 0;
                }
                break;
        }
        p++;
    }
    return sb_append_char(sb, '"');
}

static int is_identifier_key(const char* key) {
    size_t i;
    if (!key || !key[0]) return 0;
    if (!(isalpha((unsigned char)key[0]) || key[0] == '_')) return 0;
    for (i = 1; key[i]; i++) {
        if (!(isalnum((unsigned char)key[i]) || key[i] == '_')) return 0;
    }
    return 1;
}

static int serialize_value(const DataNode* node, StringBuilder* sb, int pretty, int depth, int as_json);
static int serialize_expr(const XonExpr* expr, StringBuilder* sb, int pretty, int depth, int as_json);
static int serialize_params(const DataNode* params, StringBuilder* sb) {
    const DataNode* current = params ? params->data.aggregate.value : NULL;
    if (!sb_append_char(sb, '(')) return 0;
    while (current) {
        if (current->type == TYPE_STRING) {
            if (!sb_append_str(sb, current->data.s_val ? current->data.s_val : "")) return 0;
        }
            if (current->next && !sb_append_str(sb, ", ")) return 0;
            current = current->next;
    }
    return sb_append_char(sb, ')');
}

static int serialize_expr(const XonExpr* expr, StringBuilder* sb, int pretty, int depth, int as_json) {
    (void)pretty;
    (void)depth;
    if (!expr) return 0;

    switch (expr->kind) {
        case XON_EXPR_IDENTIFIER:
            return sb_append_str(sb, expr->u.identifier_name ? expr->u.identifier_name : "");
        case XON_EXPR_BINARY: {
            const char* op = "";
            switch (expr->u.binary.op) {
                case XON_EXPR_OP_OR: op = "||"; break;
                case XON_EXPR_OP_AND: op = "&&"; break;
                case XON_EXPR_OP_EQ: op = "=="; break;
                case XON_EXPR_OP_NEQ: op = "!="; break;
                case XON_EXPR_OP_LT: op = "<"; break;
                case XON_EXPR_OP_LTE: op = "<="; break;
                case XON_EXPR_OP_GT: op = ">"; break;
                case XON_EXPR_OP_GTE: op = ">="; break;
                case XON_EXPR_OP_ADD: op = "+"; break;
                case XON_EXPR_OP_SUB: op = "-"; break;
                case XON_EXPR_OP_MUL: op = "*"; break;
                case XON_EXPR_OP_DIV: op = "/"; break;
                case XON_EXPR_OP_MOD: op = "%"; break;
                case XON_EXPR_OP_NULLISH: op = "??"; break;
                default: op = "?"; break;
            }
            if (!serialize_value(expr->u.binary.left, sb, pretty, depth, as_json)) return 0;
            if (!sb_append_char(sb, ' ')) return 0;
            if (!sb_append_str(sb, op)) return 0;
            if (!sb_append_char(sb, ' ')) return 0;
            return serialize_value(expr->u.binary.right, sb, pretty, depth, as_json);
        }
        case XON_EXPR_UNARY: {
            const char* op = expr->u.unary.op == XON_EXPR_OP_NOT ? "!" : (expr->u.unary.op == XON_EXPR_OP_NEG ? "-" : "+");
            if (!sb_append_str(sb, op)) return 0;
            return serialize_value(expr->u.unary.operand, sb, pretty, depth, as_json);
        }
        case XON_EXPR_MEMBER:
            if (!serialize_value(expr->u.member.object, sb, pretty, depth, as_json)) return 0;
            if (!sb_append_char(sb, '.')) return 0;
            return sb_append_str(sb, expr->u.member.member ? expr->u.member.member : "");
        case XON_EXPR_TERNARY:
            if (!serialize_value(expr->u.ternary.cond, sb, pretty, depth, as_json)) return 0;
            if (!sb_append_str(sb, " ? ")) return 0;
            if (!serialize_value(expr->u.ternary.then_expr, sb, pretty, depth, as_json)) return 0;
            if (!sb_append_str(sb, " : ")) return 0;
            return serialize_value(expr->u.ternary.else_expr, sb, pretty, depth, as_json);
        case XON_EXPR_IF:
            if (!sb_append_str(sb, "if (")) return 0;
            if (!serialize_value(expr->u.ternary.cond, sb, pretty, depth, as_json)) return 0;
            if (!sb_append_str(sb, ") ")) return 0;
            if (!serialize_value(expr->u.ternary.then_expr, sb, pretty, depth, as_json)) return 0;
            if (!sb_append_str(sb, " else ")) return 0;
            return serialize_value(expr->u.ternary.else_expr, sb, pretty, depth, as_json);
        case XON_EXPR_CALL:
            if (!serialize_value(expr->u.call.callee, sb, pretty, depth, as_json)) return 0;
            if (!sb_append_char(sb, '(')) return 0;
            if (expr->u.call.args) {
                const DataNode* arg = expr->u.call.args;
                while (arg) {
                    if (!serialize_value(arg, sb, pretty, depth, as_json)) return 0;
                    arg = arg->next;
                    if (arg && !sb_append_str(sb, ", ")) return 0;
                }
            }
            return sb_append_char(sb, ')');
        case XON_EXPR_FUNCTION:
            if (!serialize_params(expr->u.function.params, sb)) return 0;
            if (!sb_append_str(sb, " => ")) return 0;
            return serialize_value(expr->u.function.body, sb, pretty, depth, as_json);
        default:
            return sb_append_str(sb, "<expr>");
    }
}

static int serialize_object(const DataNode* node, StringBuilder* sb, int pretty, int depth, int as_json) {
    const DataNode* pair = node->data.aggregate.value;
    if (!sb_append_char(sb, '{')) return 0;

    if (pair) {
        if (pretty && !sb_append_char(sb, '\n')) return 0;
        while (pair) {
            if (pair->type == TYPE_DECL) {
                if (as_json) {
                    pair = pair->next;
                    continue;
                }

                if (pretty && !sb_append_indent(sb, depth + 1)) return 0;
                if (!sb_append_str(sb, pair->data.declaration.is_const ? "const " : "let ")) return 0;
                if (!sb_append_str(sb, pair->data.declaration.name ? pair->data.declaration.name : "")) return 0;
                if (!sb_append_str(sb, " = ")) return 0;
                if (!serialize_value(pair->data.declaration.init_expr, sb, pretty, depth + 1, as_json)) return 0;
            } else if (pair->type == TYPE_OBJECT) {
                const char* key = NULL;

                if (pretty && !sb_append_indent(sb, depth + 1)) return 0;

                if (pair->data.aggregate.key && pair->data.aggregate.key->type == TYPE_STRING) {
                    key = pair->data.aggregate.key->data.s_val;
                }
                if (!key) key = "";

                if (as_json || !is_identifier_key(key)) {
                    if (!sb_append_escaped_string(sb, key)) return 0;
                } else {
                    if (!sb_append_str(sb, key)) return 0;
                }

                if (!sb_append_str(sb, pretty ? ": " : ":")) return 0;
                if (!serialize_value(pair->data.aggregate.value, sb, pretty, depth + 1, as_json)) return 0;

            } else {
                pair = pair->next;
                continue;
            }

            if (pair->next && !sb_append_char(sb, ',')) return 0;
            if (pretty && !sb_append_char(sb, '\n')) return 0;
            pair = pair->next;
        }
        if (pretty && !sb_append_indent(sb, depth)) return 0;
    }

    return sb_append_char(sb, '}');
}

static int serialize_list(const DataNode* node, StringBuilder* sb, int pretty, int depth, int as_json) {
    const DataNode* item = node->data.aggregate.value;
    if (!sb_append_char(sb, '[')) return 0;

    if (item) {
        if (pretty && !sb_append_char(sb, '\n')) return 0;
        while (item) {
            if (pretty && !sb_append_indent(sb, depth + 1)) return 0;
            if (!serialize_value(item, sb, pretty, depth + 1, as_json)) return 0;
            if (item->next && !sb_append_char(sb, ',')) return 0;
            if (pretty && !sb_append_char(sb, '\n')) return 0;
            item = item->next;
        }
        if (pretty && !sb_append_indent(sb, depth)) return 0;
    }

    return sb_append_char(sb, ']');
}

static int serialize_value(const DataNode* node, StringBuilder* sb, int pretty, int depth, int as_json) {
    char numbuf[64];
    if (!node) return sb_append_str(sb, "null");

    switch (node->type) {
        case TYPE_OBJECT:
            return serialize_object(node, sb, pretty, depth, as_json);
        case TYPE_LIST:
            return serialize_list(node, sb, pretty, depth, as_json);
        case TYPE_EXPR:
            if (as_json) return sb_append_str(sb, "null");
            return serialize_expr(node->data.expr, sb, pretty, depth, as_json);
        case TYPE_FUNCTION:
            if (as_json) return sb_append_str(sb, "null");
            return sb_append_str(sb, "<function>");
        case TYPE_DECL:
            if (as_json) return sb_append_str(sb, "null");
            if (node->data.declaration.is_const) {
                if (!sb_append_str(sb, "const ")) return 0;
            } else {
                if (!sb_append_str(sb, "let ")) return 0;
            }
            if (!sb_append_str(sb, node->data.declaration.name ? node->data.declaration.name : "")) return 0;
            if (!sb_append_str(sb, " = ")) return 0;
            return serialize_value(node->data.declaration.init_expr, sb, pretty, depth, as_json);
        case TYPE_STRING:
            return sb_append_escaped_string(sb, node->data.s_val ? node->data.s_val : "");
        case TYPE_NUMBER:
            snprintf(numbuf, sizeof(numbuf), "%.17g", node->data.n_val);
            return sb_append_str(sb, numbuf);
        case TYPE_BOOL:
            return sb_append_str(sb, node->data.b_val ? "true" : "false");
        case TYPE_NULL:
            return sb_append_str(sb, "null");
    }
    return 0;
}

static DataNode* object_pair_at(const XonValue* obj, size_t index) {
    DataNode* pair;
    size_t i = 0;
    if (!obj || obj->type != TYPE_OBJECT) return NULL;

    pair = obj->data.aggregate.value;
    while (pair) {
        if (i == index) return pair;
        pair = pair->next;
        i++;
    }
    return NULL;
}

XonValue* xonify(const char* filename) {
    FILE* f;
    DataNode* root;

    if (!filename) return NULL;
    xon_logger_init("xon");
    xon_log_info("api", "Parsing file: %s", filename);
    f = fopen(filename, "r");
    if (!f) {
        xon_log_error("api", "Failed to open file: %s", filename);
        return NULL;
    }

    root = parse_stream(f);
    fclose(f);
    return root;
}

XonValue* xonify_string(const char* str) {
    FILE* tmp;
    DataNode* root;
    if (!str) return NULL;

    xon_logger_init("xon");
    xon_log_info("api", "Parsing input string");
    tmp = tmpfile();
    if (!tmp) {
        xon_log_error("api", "Failed to allocate temporary file for string parsing");
        return NULL;
    }
    fputs(str, tmp);
    rewind(tmp);

    root = parse_stream(tmp);
    fclose(tmp);
    return root;
}

void xon_free(XonValue* value) {
    free_xon_ast(value);
}

XonType xon_get_type(const XonValue* value) {
    if (!value) return XON_TYPE_NULL;
    switch (value->type) {
        case TYPE_NULL: return XON_TYPE_NULL;
        case TYPE_BOOL: return XON_TYPE_BOOL;
        case TYPE_NUMBER: return XON_TYPE_NUMBER;
        case TYPE_STRING: return XON_TYPE_STRING;
        case TYPE_OBJECT: return XON_TYPE_OBJECT;
        case TYPE_LIST: return XON_TYPE_LIST;
        case TYPE_EXPR:
        case TYPE_DECL:
        case TYPE_FUNCTION:
            return XON_TYPE_NULL;
    }
    return XON_TYPE_NULL;
}

int xon_is_null(const XonValue* value) {
    return value && value->type == TYPE_NULL;
}

int xon_is_bool(const XonValue* value) {
    return value && value->type == TYPE_BOOL;
}

int xon_is_number(const XonValue* value) {
    return value && value->type == TYPE_NUMBER;
}

int xon_is_string(const XonValue* value) {
    return value && value->type == TYPE_STRING;
}

int xon_is_object(const XonValue* value) {
    return value && value->type == TYPE_OBJECT;
}

int xon_is_list(const XonValue* value) {
    return value && value->type == TYPE_LIST;
}

int xon_get_bool(const XonValue* value) {
    return (value && value->type == TYPE_BOOL) ? value->data.b_val : 0;
}

double xon_get_number(const XonValue* value) {
    return (value && value->type == TYPE_NUMBER) ? value->data.n_val : 0.0;
}

const char* xon_get_string(const XonValue* value) {
    return (value && value->type == TYPE_STRING) ? value->data.s_val : NULL;
}

XonValue* xon_object_get(const XonValue* obj, const char* key) {
    return xon_get_key_internal((DataNode*)obj, key);
}

int xon_object_has(const XonValue* obj, const char* key) {
    return xon_object_get(obj, key) != NULL;
}

size_t xon_object_size(const XonValue* obj) {
    DataNode* current;
    size_t count = 0;
    if (!obj || obj->type != TYPE_OBJECT) return 0;
    current = obj->data.aggregate.value;
    while (current) {
        count++;
        current = current->next;
    }
    return count;
}

const char* xon_object_key_at(const XonValue* obj, size_t index) {
    DataNode* pair = object_pair_at(obj, index);
    if (!pair || !pair->data.aggregate.key || pair->data.aggregate.key->type != TYPE_STRING) return NULL;
    return pair->data.aggregate.key->data.s_val;
}

XonValue* xon_object_value_at(const XonValue* obj, size_t index) {
    DataNode* pair = object_pair_at(obj, index);
    if (!pair) return NULL;
    return pair->data.aggregate.value;
}

XonValue* xon_list_get(const XonValue* list, size_t index) {
    DataNode* current;
    size_t i = 0;
    if (!list || list->type != TYPE_LIST) return NULL;
    current = list->data.aggregate.value;
    while (current) {
        if (i == index) return current;
        current = current->next;
        i++;
    }
    return NULL;
}

size_t xon_list_size(const XonValue* list) {
    DataNode* current;
    size_t count = 0;
    if (!list || list->type != TYPE_LIST) return 0;
    current = list->data.aggregate.value;
    while (current) {
        count++;
        current = current->next;
    }
    return count;
}

char* xon_to_json(const XonValue* value, int pretty) {
    StringBuilder sb;
    if (!sb_init(&sb)) return NULL;
    if (!serialize_value((const DataNode*)value, &sb, pretty ? 1 : 0, 0, 1)) {
        free(sb.data);
        return NULL;
    }
    return sb.data;
}

char* xon_to_xon(const XonValue* value, int pretty) {
    StringBuilder sb;
    if (!sb_init(&sb)) return NULL;
    if (!serialize_value((const DataNode*)value, &sb, pretty ? 1 : 0, 0, 0)) {
        free(sb.data);
        return NULL;
    }
    return sb.data;
}

void xon_string_free(char* str) {
    free(str);
}

void xon_print(const XonValue* value) {
    print_ast((const DataNode*)value, 0);
}

int xon_set_log_directory(const char* directory) {
    return xon_logger_set_directory(directory);
}

void xon_set_log_level(XonLogLevel level) {
    xon_logger_set_level(level);
}

void xon_enable_stderr_logging(int enabled) {
    xon_logger_enable_stderr(enabled);
}

void xon_shutdown_logging(void) {
    xon_logger_shutdown();
}
