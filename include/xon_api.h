#ifndef XON_API_H
#define XON_API_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque pointer - actual definition is DataNode from xon.c
struct DataNode;
typedef struct DataNode XonValue;

// Type enumeration for runtime type checking
typedef enum {
    XON_TYPE_NULL,
    XON_TYPE_BOOL,
    XON_TYPE_NUMBER,
    XON_TYPE_STRING,
    XON_TYPE_OBJECT,
    XON_TYPE_LIST
} XonType;

typedef enum {
    XON_LOG_DEBUG = 0,
    XON_LOG_INFO = 1,
    XON_LOG_WARN = 2,
    XON_LOG_ERROR = 3
} XonLogLevel;

// ============ Core API (Branded) ============

// Parse a .xon file from path (Brand: xonify)
XonValue* xonify(const char* filename);

// Parse .xon from a string (Brand: xonify_string)
XonValue* xonify_string(const char* xon_string);

// Evaluate parsed XON expression/object with runtime semantics (variables, functions, built-ins).
// Caller must free the returned XonValue with xon_free().
XonValue* xon_eval(const XonValue* value);

// Free memory
void xon_free(XonValue* value);

// ============ Type Checking ============

XonType xon_get_type(const XonValue* value);
int xon_is_null(const XonValue* value);
int xon_is_bool(const XonValue* value);
int xon_is_number(const XonValue* value);
int xon_is_string(const XonValue* value);
int xon_is_object(const XonValue* value);
int xon_is_list(const XonValue* value);

// ============ Value Access ============

// Get primitive values (returns 0/NULL on type mismatch)
int xon_get_bool(const XonValue* value);
double xon_get_number(const XonValue* value);
const char* xon_get_string(const XonValue* value);

// ============ Object Operations ============

// Get value by key (returns NULL if not found)
XonValue* xon_object_get(const XonValue* obj, const char* key);

// Check if key exists
int xon_object_has(const XonValue* obj, const char* key);

// Get number of keys
size_t xon_object_size(const XonValue* obj);

// Get key at index (returns NULL if out of bounds)
const char* xon_object_key_at(const XonValue* obj, size_t index);

// Get value at index (returns NULL if out of bounds)
XonValue* xon_object_value_at(const XonValue* obj, size_t index);

// ============ List Operations ============

// Get list element by index (returns NULL if out of bounds)
XonValue* xon_list_get(const XonValue* list, size_t index);

// Get list length
size_t xon_list_size(const XonValue* list);

// ============ Serialization ============

// Convert a parsed value to JSON string. Caller must free with xon_string_free().
char* xon_to_json(const XonValue* value, int pretty);

// Convert a parsed value to Xon string. Caller must free with xon_string_free().
char* xon_to_xon(const XonValue* value, int pretty);

// Free strings returned by xon_to_json() / xon_to_xon().
void xon_string_free(char* str);

// ============ Debugging ============

// Print AST structure
void xon_print(const XonValue* value);

// ============ Logging ============

// Set log directory (created if missing). Returns 1 on success, 0 on error.
int xon_set_log_directory(const char* directory);

// Set minimum log level.
void xon_set_log_level(XonLogLevel level);

// Enable/disable stderr mirroring (warnings/errors).
void xon_enable_stderr_logging(int enabled);

// Flush and close logger resources.
void xon_shutdown_logging(void);

#ifdef __cplusplus
}
#endif

#endif // XON_API_H
