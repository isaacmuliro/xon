#include <napi.h>
extern "C" {
    #include "../include/xon_api.h"
}
#include <string>

// Helper to convert XonValue to JS
Napi::Value ConvertToJS(Napi::Env env, const XonValue* value);

Napi::Value ConvertToJS(Napi::Env env, const XonValue* value) {
    if (!value) return env.Null();

    switch (xon_get_type(value)) {
        case XON_TYPE_NULL:
            return env.Null();
        
        case XON_TYPE_BOOL:
            return Napi::Boolean::New(env, xon_get_bool(value));
        
        case XON_TYPE_NUMBER:
            return Napi::Number::New(env, xon_get_number(value));
        
        case XON_TYPE_STRING: {
            const char* str = xon_get_string(value);
            return str ? Napi::String::New(env, str) : env.Null();
        }
        
        case XON_TYPE_OBJECT: {
            Napi::Object obj = Napi::Object::New(env);
            size_t size = xon_object_size(value);

            for (size_t i = 0; i < size; i++) {
                const char* key = xon_object_key_at(value, i);
                XonValue* item = xon_object_value_at(value, i);
                if (key) {
                    obj.Set(key, ConvertToJS(env, item));
                }
            }

            return obj;
        }
        
        case XON_TYPE_LIST: {
            size_t size = xon_list_size(value);
            Napi::Array arr = Napi::Array::New(env, size);
            
            for (size_t i = 0; i < size; i++) {
                XonValue* item = xon_list_get(value, i);
                arr[i] = ConvertToJS(env, item);
            }
            return arr;
        }
        
        default:
            return env.Null();
    }
}

static bool GetPrettyOption(const Napi::CallbackInfo& info, size_t index, bool default_value, bool* ok) {
    Napi::Env env = info.Env();

    *ok = 1;
    if (info.Length() <= index) return default_value;
    if (!info[index].IsBoolean()) {
        Napi::TypeError::New(env, "Boolean expected for pretty option").ThrowAsJavaScriptException();
        *ok = 0;
        return default_value;
    }

    return info[index].As<Napi::Boolean>().Value();
}

// Branded: xonify() - Parse file function
Napi::Value Xonify(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string path = info[0].As<Napi::String>();
    XonValue* result = xonify(path.c_str());
    
    if (!result) {
        Napi::Error::New(env, "Failed to parse file").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Value jsValue = ConvertToJS(env, result);
    xon_free(result);
    return jsValue;
}

// Branded: xonifyString() - Parse string function
Napi::Value XonifyString(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string content = info[0].As<Napi::String>();
    XonValue* result = xonify_string(content.c_str());
    
    if (!result) {
        Napi::Error::New(env, "Failed to parse string").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Value jsValue = ConvertToJS(env, result);
    xon_free(result);
    return jsValue;
}

Napi::Value XonifyToXon(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    bool ok = 0;
    bool pretty = 1;
    std::string path;
    XonValue* parsed = NULL;
    char* rendered = NULL;
    Napi::Value result = env.Null();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    pretty = GetPrettyOption(info, 1, 1, &ok);
    if (!ok) return env.Null();

    path = info[0].As<Napi::String>();
    parsed = xonify(path.c_str());
    if (!parsed) {
        Napi::Error::New(env, "Failed to parse file").ThrowAsJavaScriptException();
        return env.Null();
    }

    rendered = xon_to_xon(parsed, pretty ? 1 : 0);
    xon_free(parsed);
    if (!rendered) {
        Napi::Error::New(env, "Failed to serialize Xon output").ThrowAsJavaScriptException();
        return env.Null();
    }

    result = Napi::String::New(env, rendered);
    xon_string_free(rendered);
    return result;
}

Napi::Value XonifyToJson(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    bool ok = 0;
    bool pretty = 1;
    std::string path;
    XonValue* parsed = NULL;
    char* rendered = NULL;
    Napi::Value result = env.Null();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    pretty = GetPrettyOption(info, 1, 1, &ok);
    if (!ok) return env.Null();

    path = info[0].As<Napi::String>();
    parsed = xonify(path.c_str());
    if (!parsed) {
        Napi::Error::New(env, "Failed to parse file").ThrowAsJavaScriptException();
        return env.Null();
    }

    rendered = xon_to_json(parsed, pretty ? 1 : 0);
    xon_free(parsed);
    if (!rendered) {
        Napi::Error::New(env, "Failed to serialize JSON output").ThrowAsJavaScriptException();
        return env.Null();
    }

    result = Napi::String::New(env, rendered);
    xon_string_free(rendered);
    return result;
}

Napi::Value XonEvalToXon(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    bool ok = 0;
    bool pretty = 1;
    std::string path;
    XonValue* parsed = NULL;
    XonValue* evaluated = NULL;
    char* rendered = NULL;
    Napi::Value result = env.Null();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    pretty = GetPrettyOption(info, 1, 1, &ok);
    if (!ok) return env.Null();

    path = info[0].As<Napi::String>();
    parsed = xonify(path.c_str());
    if (!parsed) {
        Napi::Error::New(env, "Failed to parse file").ThrowAsJavaScriptException();
        return env.Null();
    }

    evaluated = xon_eval(parsed);
    xon_free(parsed);
    if (!evaluated) {
        Napi::Error::New(env, "Failed to evaluate file").ThrowAsJavaScriptException();
        return env.Null();
    }

    rendered = xon_to_xon(evaluated, pretty ? 1 : 0);
    xon_free(evaluated);
    if (!rendered) {
        Napi::Error::New(env, "Failed to serialize evaluation output").ThrowAsJavaScriptException();
        return env.Null();
    }

    result = Napi::String::New(env, rendered);
    xon_string_free(rendered);
    return result;
}

// Module initialization
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("xonify", Napi::Function::New(env, Xonify));
    exports.Set("xonifyString", Napi::Function::New(env, XonifyString));
    exports.Set("xonifyToXon", Napi::Function::New(env, XonifyToXon));
    exports.Set("xonifyToJson", Napi::Function::New(env, XonifyToJson));
    exports.Set("xonEvalToXon", Napi::Function::New(env, XonEvalToXon));
    return exports;
}

NODE_API_MODULE(xon, Init)
