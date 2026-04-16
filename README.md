# Xon Language

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C](https://img.shields.io/badge/language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

**Xon** is a modern, developer-friendly configuration language that extends JSON with practical features for real-world use cases. Built in C with a hand-written lexer and LALR(1) parser.

## Features

Xon provides all the structure of JSON, plus:

- **Comments** - Line (`//`) and block (`/* ... */`) comments for documentation
- **Hexadecimal Numbers** - Native support for hex literals (`0xFF`, `0x1A`)
- **Trailing Commas** - No more syntax errors from trailing commas
- **Unquoted Keys** - Clean syntax: `name: "value"` instead of `"name": "value"`
- **Escape Sequences** - Full support for `\n`, `\t`, `\"`, `\\`, etc.
- **Line-Based Error Reporting** - Precise error messages with line numbers

## What Developers Should Expect in v1.0.0

Xon v1.0.0 is ready for production use as a native config parser/runtime for trusted application configuration files.

What is stable in v1.0.0:
- Node package `@xerxisfy/xon` with native addon and CLI (`xon`).
- Parsing and serialization for JSON-compatible data plus Xon syntax extensions.
- CLI workflows for `validate`, `format`, `convert`, and `eval`.
- Runtime expressions (`let/const`, operators, functions, closures, built-ins).
- C API for parse, eval, traversal, and serialization.

Recommended production usage:
- Use Xon for application and service configuration files stored in your own repositories.
- Run `xon validate` and `xon format` in CI to enforce config quality.
- Parse with Xon, then validate schema at the app layer.
- Treat runtime eval as trusted-input-only unless you add process isolation and strict limits.

What is not yet in v1.0.0:
- First-class module/import/include system.
- Built-in schema validation engine.
- Full advanced literal set (for example binary/octal literals).

##  Build-Time JSON Workflow (Recommended for Web Apps)

Browsers and most frontend frameworks do not natively read `.xon` files.  
The production-safe pattern is to compile `.xon` to `.json` at build time.

Xon CLI includes `build` for this:

```bash
# zero-config default
# input:  config/app.xon
# output: src/generated/app-config.json
npx xon build

# explicit single file
npx xon build config/app.xon src/generated/app-config.json

# convert all .xon files in a directory
npx xon build config src/generated
```

You can also add a project config so developers just run `xon build`:

```json
{
  "build": {
    "input": "config",
    "output": "src/generated"
  }
}
```

Save that as `xon.config.json` in your project root.

Then import generated JSON in your app:

```javascript
import appConfig from "./generated/app-config.json";
```

Example package scripts:

```json
{
  "scripts": {
    "xon:build": "xon build config src/generated",
    "build": "npm run xon:build && vite build"
  }
}
```

## Quick Example

```javascript
{
    // Server configuration
    server_name: "XonServer",
    max_memory: 0xFF,           // 255 in hex
    is_active: true,
    features: [
        "fast",
        "simple",
        "powerful",             // trailing comma OK!
    ],
    database: {
        host: "localhost",
        port: 5432,
        ssl: true,
    }
}
```

## Getting Started

### Prerequisites

- GCC compiler (or any C99-compatible compiler)
- Make (optional, for build automation)
- Lemon Parser Generator (included in `tools/`)

### Building from Source

```bash
# Clone the repository
git clone https://github.com/xerxisfy/xon.git
cd xon

# Build CLI + shared library
./build.sh

# Run
./xon validate tests/test.xon
./xon format tests/test.xon
./xon convert tests/test.xon /tmp/test.json
./xon eval tests/test.xon
```

### Using Make

```bash
make          # Build the project
make test     # Run tests
make clean    # Clean build artifacts
```

##  Language Specification

### Data Types

| Type | Example | Description |
|------|---------|-------------|
| **String** | `"Hello\nWorld"` | UTF-8 text with escape sequences |
| **Number** | `42`, `3.14`, `0xFF` | Decimal, floating-point, or hexadecimal |
| **Boolean** | `true`, `false` | Logical values |
| **Null** | `null` | Absence of value |
| **Object** | `{ key: value }` | Key-value mappings |
| **List** | `[1, 2, 3]` | Ordered collections |

### Syntax Rules

1. **Keys** can be quoted (`"name"`) or unquoted (`name`) if they're valid identifiers
2. **Comments** support `//`, `#`, and `/* block */` styles
3. **Trailing commas** are allowed in objects and lists
4. **Hex numbers** must start with `0x` or `0X`
5. **Escape sequences**: `\n` (newline), `\t` (tab), `\"` (quote), `\\` (backslash), `\r` (carriage return)

### Grammar Overview

```
root       ::= object | list
object     ::= '{' pair_list? '}'
list       ::= '[' value_list? ']'
pair       ::= (STRING | IDENTIFIER) ':' value
value      ::= STRING | NUMBER | TRUE | FALSE | NULL_VAL | object | list
            | expr | declaration

declaration ::= ('let' | 'const') IDENTIFIER '=' expr

expr       ::= if_statement
            | ternary
            | binary
            | unary
            | call
            | function
            | IDENTIFIER
            | '(' expr ')'

if_statement ::= 'if' '(' expr ')' expr 'else' expr
ternary      ::= expr '?' expr ':' expr
binary       ::= expr ('+' | '-' | '*' | '/' | '%' | '==' | '!=' | '<' | '<=' | '>' | '>=' | '&&' | '||' | '??') expr
unary        ::= ('!' | '+' | '-') expr
call         ::= expr '(' arg_list? ')'
function     ::= '(' param_list? ')' '=>' expr
param_list   ::= IDENTIFIER (',' IDENTIFIER)*
arg_list     ::= expr (',' expr)*
```

## 🔧 API Reference

### Parsing a File

```c
#include <stdio.h>
#include "lexer.h"
#include "xon.c"

int main() {
    FILE* f = fopen("config.xon", "r");
    void* parser = xonParserAlloc(malloc);
    DataNode* root = NULL;
    
    // ... token loop (see main.c for complete example)
    
    xonParserFree(parser, free);
    fclose(f);
    
    if (root) {
        // Use the data
        free_xon_ast(root);
    }
    
    return 0;
}
```

### Runtime Evaluation (Round-1)

Parse, then execute runtime expressions:

```c
#include <stdio.h>
#include "include/xon_api.h"

int main(void) {
    XonValue* ast = xonify("examples/config.xon");
    XonValue* evaluated;

    if (!ast) return 1;
    evaluated = xon_eval(ast);
    if (!evaluated) {
        xon_free(ast);
        return 1;
    }

    char* rendered = xon_to_xon(evaluated, 1);
    if (rendered) {
        puts(rendered);
        xon_string_free(rendered);
    }

    xon_free(evaluated);
    xon_free(ast);
    return 0;
}
```

Round-1 runtime support includes:

- `let` / `const` declarations, forward references, and scoped lookup.
- Arithmetic and boolean expressions with precedence.
- Conditional (`if` / ternary) and nullish (`??`) operators.
- Anonymous functions, closures, and call expressions.
- Built-ins: `abs`, `len`, `max`, `min`, `str`, `upper`, `lower`, `keys`, `has`, `env`.

### Visitor Pattern

Query parsed data dynamically:

```c
// Get a value from an object by key
DataNode* xon_get_key(DataNode* obj, const char* key);

// Example usage
DataNode* root = /* parsed AST */;
DataNode* server = xon_get_key(root, "server_name");
if (server && server->type == TYPE_STRING) {
    printf("Server: %s\n", server->data.s_val);
}
```

### AST Structure

```c
typedef struct DataNode {
    enum DataType type;         // TYPE_OBJECT, TYPE_LIST, TYPE_STRING, etc.
    struct DataNode *next;      // Sibling in linked list
    union {
        char *s_val;            // String data
        double n_val;           // Numeric data
        int b_val;              // Boolean data (1 or 0)
        struct {
            struct DataNode *key;
            struct DataNode *value;
        } aggregate;            // Object/List container
    } data;
} DataNode;
```

### Data Types

```c
enum DataType {
    TYPE_OBJECT,
    TYPE_LIST,
    TYPE_STRING,
    TYPE_NUMBER,
    TYPE_BOOL,
    TYPE_NULL
};
```

### Memory Management

Always free the AST after use to prevent memory leaks:

```c
void free_xon_ast(DataNode* node);  // Recursive cleanup
```

### Utility Functions

```c
// Print AST structure (debugging)
void print_ast(DataNode* node, int depth);

// Create a new node
DataNode* new_node(enum DataType type);

// Link nodes in a list
DataNode* link_node(DataNode* head, DataNode* item);
```

## 🧪 Testing

### Running Tests

```bash
# End-to-end C + Python tests
./scripts/run_tests.sh

# Or via make
make test
```

### Expected Output

```text
=== Xon Test Suite ===
All tests passed.
{"status": "ok", "python_binding": "passed"}
```

### Logging System

Xon now includes a file-based logging mechanism for CLI and parser/runtime errors.

- Default log directory: `logs/`
- Log file format: `logs/xon-cli-YYYY-MM-DD.log` (CLI) and `logs/xon-YYYY-MM-DD.log` (API/runtime)
- Parser syntax errors, lexer errors, warnings, and operational info are written to log files.

Environment variables:

- `XON_LOG_DIR` - override log directory path
- `XON_LOG_LEVEL` - one of `debug`, `info`, `warn`, `error`
- `XON_LOG_STDERR` - `0` (default) disables logger stderr mirroring, `1` enables mirroring

Public API controls:

```c
int xon_set_log_directory(const char* directory);
void xon_set_log_level(XonLogLevel level);
void xon_enable_stderr_logging(int enabled);
void xon_shutdown_logging(void);
```

### Creating Test Files

Example test file (`test.xon`):

```javascript
{
    // Test all features
    string_test: "Hello\nWorld",
    number_test: 42,
    hex_test: 0xFF,
    bool_test: true,
    null_test: null,
    list_test: [1, 2, 3,],
    object_test: {
        nested: "value",
    }
}
```

##  Project Structure

```
xon/
├── src/
│   ├── main.c      # CLI driver
│   ├── lexer.c     # Tokenizer implementation
│   ├── lexer.h     # Lexer interface
│   ├── logger.c    # File-based logging system
│   ├── logger.h    # Logger interface (internal)
│   ├── xon.lemon   # Grammar specification
│   ├── xon.c       # Generated parser
│   └── xon_api.c   # Public C API implementation
├── include/
│   └── xon_api.h   # Public API header
├── bindings/
│   ├── xon_node.cpp
│   └── python/
│       └── xon.py
├── test.xon        # Test configuration file
├── tools/
│   └── lemon       # Lemon parser generator
├── docs.md         # Long-lived technical documentation
├── release-docs.md # Release runbook and publishing instructions
├── vscode-xon/     # VS Code extension package
├── xon-language-server/ # LSP package for other IDEs
└── README.md       # This file
```

##  Contributing

We welcome contributions! Here's how you can help:

### Reporting Bugs

Open an issue with:
- Clear title describing the problem
- Steps to reproduce
- Expected vs actual behavior
- Your environment (OS, compiler version)
- Sample `.xon` file that triggers the bug

### Suggesting Features

Open an issue or discussion with:
- Use case description
- Proposed syntax/behavior
- Examples of how it would work

## 📘 Additional Docs

- Long-lived technical reference: `docs.md`
- Release and publish runbook: `release-docs.md`

##  Release Publishing

### npm (core package)

```bash
npm run pack:preview
npm publish --access public
```

### npm (language server package)

```bash
cd xon-language-server
npm publish --access public
```

### PyPI

```bash
python3 -m pip install --upgrade build twine
python3 -m build --wheel bindings/python --outdir dist-python
python3 -m twine check dist-python/*
python3 -m twine upload dist-python/*
```

### VS Code Marketplace

```bash
cd vscode-xon
npm install
npm run package
npm run publish:vsce
```

### Release preflight

Run this before tagging or publishing:

```bash
./scripts/release_check.sh
```

### Pull Requests

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Regenerate parser if grammar changed: `./tools/lemon src/xon.lemon`
5. Test your changes: `./scripts/run_tests.sh`
6. Commit with clear messages (`git commit -m 'Add amazing feature'`)
7. Push to your fork (`git push origin feature/amazing-feature`)
8. Open a Pull Request

### Code Style

- Use 4 spaces for indentation
- Follow existing naming conventions
- Add comments for complex logic
- Update documentation for new features
- Ensure code compiles without warnings (`-Wall -Wextra`)

##  Roadmap

### Version 1.0 (Current)
- [x] Basic JSON-compatible parsing
- [x] Comments support
- [x] Hexadecimal numbers
- [x] Trailing commas
- [x] Unquoted keys
- [x] Visitor pattern API
- [x] Line-based error reporting

### Future Versions
- [ ] Error recovery (continue parsing after errors)
- [ ] Multi-line strings (triple quotes `"""`)
- [ ] Include/import system
- [ ] Schema validation
- [ ] Binary number literals (`0b1010`)
- [ ] Octal number literals (`0o755`)
- [ ] Scientific notation (`1.23e-4`)
- [ ] Unicode escape sequences (`\u{1F600}`)
- [ ] Date/Time literals
- [ ] Shared library build (`libxon.so`)
- [ ] Language bindings (Python, JavaScript, Rust)
- [ ] VSCode syntax highlighting extension
- [ ] Online playground/REPL
- [ ] Pretty-printer/formatter
- [ ] JSON converter (`xon2json`, `json2xon`)

##  Comparison with Other Formats

| Feature | JSON | JSON5 | TOML | YAML | **Xon** |
|---------|------|-------|------|------|---------|
| Comments | ❌ | ✅ | ✅ | ✅ | ✅ |
| Trailing Commas | ❌ | ✅ | N/A | N/A | ✅ |
| Unquoted Keys | ❌ | ✅ | ✅ | ✅ | ✅ |
| Hex Numbers | ❌ | ✅ | ✅ | ❌ | ✅ |
| Multi-line Strings | ❌ | ✅ | ✅ | ✅ | 🔜 |
| Native C API | ❌ | ❌ | ❌ | ❌ | ✅ |
| Simple Grammar | ✅ | ✅ | ❌ | ❌ | ✅ |

##  License

This project is licensed under the MIT License - see below for details:

```
MIT License

Copyright (c) 2025 Xon Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Acknowledgments

- **Lemon Parser Generator** by D. Richard Hipp (creator of SQLite)
- Inspired by JSON, JSON5, and TOML
- Built with guidance from compiler design principles
- Community contributors (thank you!)

## 📞 Contact & Support

- **Maintainer**: Isaac Muliro
- **Issues**: [GitHub Issues](https://github.com/xerxisfy/xon/issues)
- **Discussions**: [GitHub Discussions](https://github.com/xerxisfy/xon/discussions)
- **Documentation**: `README.md`, `docs.md`, `release-docs.md`
- **Email**: xerxisfyágmail.com

##  Show Your Support

If you find Xon useful, please consider:
- Giving it a star on GitHub 
- Sharing it with others who might benefit
- Contributing code, documentation, or ideas
- Reporting bugs and suggesting improvements

---

**Made with  by Isaac Muliro**

*"Configuration should be simple, readable, and human-friendly."*
