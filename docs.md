# Xon Long-Lived Documentation

Last updated: 2026-04-13

This file is the long-lived technical source of truth for Xon. It replaces historical fragments that previously lived across multiple markdown files that I had in while building this language from scratch.

## 1. What Xon Is

Xon (Xerxis Object Notation) is a JSON-compatible configuration language with a native C parser/runtime and first-party bindings.

Primary goals:
- Keep JSON familiarity for day-one adoption.
- Add practical configuration features used in real projects.
- Expose a small, predictable native API for embedding.
- Provide a Node package with a native addon and CLI.

## 2. Current Capability Snapshot

Implemented and verified in this repository:
- Core parser for objects, lists, strings, numbers, booleans, null.
- Comments: `//`, `#`, and `/* ... */`.
- Unquoted object keys for identifier-like keys.
- Trailing commas in objects and lists.
- Hex literals (`0xFF`, `0x1A`).
- Runtime expression evaluation (`xon_eval`) with:
  - `let` and `const`
  - arithmetic / boolean / comparison operators
  - nullish coalescing (`??`)
  - conditional forms (`if (...) ... else ...` and `?:`)
  - anonymous functions and closures (`(a, b) => a + b`)
  - built-ins: `abs`, `len`, `max`, `min`, `str`, `upper`, `lower`, `keys`, `has`, `env`
- Native serialization to JSON and Xon.
- CLI command surface for parse/validate/format/convert/eval.
- Node addon + JS wrapper + npm CLI entrypoint.
- Python ctypes binding.
- LSP/VS Code/playground scaffolds.

### 2.1 What Developers Should Expect in v1.0.0

Xon v1.0.0 is intended for production configuration workflows where input is trusted and version-controlled.

Teams adopting v1.0.0 can rely on:
- Stable parse/format/convert/eval workflows through the npm CLI.
- Stable Node API (`xonify`, `xonifyString`, `stringify`) for application config loading.
- Stable C API for embedding Xon parsing/evaluation in native services.
- Cross-language adoption path through Node and Python bindings.

Production operating model:
- Store `.xon` config in source control.
- Validate syntax in CI (`xon validate`).
- Normalize style in CI (`xon format`).
- Convert to JSON only where downstream tools require JSON.
- Apply app-level schema validation after parsing.

Operational caveats:
- Eval is powerful and should be treated as trusted-input-only unless isolated.
- Import/include semantics are not first-class language features in v1.0.0.
- Some roadmap syntax extensions are intentionally deferred to post-1.0 releases.

## 3. What Xon Is Not (Yet)

Not yet implemented as first-class language features:
- Module/import/include syntax with file resolution semantics.
- Triple-quoted multiline strings.
- Binary/octal number literal syntax.
- Full unicode escape semantics in lexer/runtime.
- Hard runtime safety policies (strict stack/time/allocation guards as language-level config).
- Policy-driven duplicate key handling modes.

## 4. Repository Layout

Important paths:
- `src/`:
  - `lexer.c`, `lexer.h`: tokenizer and token diagnostics.
  - `xon.lemon`: Lemon grammar source.
  - `xon.c`, `xon.h`: generated parser sources committed to repo.
  - `xon_api.c`: parse/eval/serialize/API implementations.
  - `main.c`: native CLI implementation.
  - `logger.c`, `logger.h`: internal logging system.
- `include/`:
  - `xon_api.h`: public C API.
  - `xon.h`: public parser token header.
- `bindings/`:
  - `xon_node.cpp`: N-API bridge.
  - `xon.d.ts`: TypeScript declarations.
  - `python/xon.py`: ctypes Python wrapper.
- `bin/xon`: npm CLI entrypoint that maps to native addon behavior.
- `build.sh`: build orchestration with parser generation fallback behavior.
- `scripts/run_tests.sh`: C + Python test run.
- `scripts/release_check.sh`: release preflight.
- `tests/test_suite.c`: C acceptance test suite.
- `test.js`: Node addon smoke tests.
- `xon-language-server/`: LSP package.
- `vscode-xon/`: VS Code extension package.
- `play.ground/`: browser playground and optional sandbox service.

## 5. Language Specification

### 5.1 Core Data Model

Root values:
- Object
- List

Primitive values:
- String
- Number (decimal, float, scientific notation through C parsing, hex literal support)
- Boolean
- Null

### 5.2 Structural Syntax

Objects:
```xon
{
  key: "value",
  "quoted-key": 42,
}
```

Lists:
```xon
[
  1,
  2,
  3,
]
```

Comments:
```xon
// line comment
# hash comment
/* block
   comment */
```

### 5.3 Runtime Expression Layer

Expression categories:
- identifiers
- unary: `!`, `+`, `-`
- binary: `+ - * / % == != < <= > >= && || ??`
- conditionals: `if (cond) a else b`, `cond ? a : b`
- function literals: `(a, b) => a + b`
- function calls: `fn(1, 2)`
- member access: `obj.field`

Object declarations:
```xon
{
  let base = 10,
  let add = (a, b) => a + b + base,
  total: add(2, 3),
}
```

Evaluation model:
- Parse produces AST-like value graph.
- `xon_eval` evaluates expressions in object/list nodes.
- Declarations populate lexical scope but are not emitted as output object keys.
- Forward references are supported via deferred initialization logic.
- Unknown identifiers may resolve via environment variables in evaluation context.

### 5.4 Built-in Functions

Built-ins and expected arguments:
- `abs(x)` -> number
- `len(x)` -> number for string/list/object
- `max(x...)` -> number
- `min(x...)` -> number
- `str(x)` -> string
- `upper(string)` -> string
- `lower(string)` -> string
- `keys(object)` -> list of key strings
- `has(object, key)` -> boolean
- `env(name)` -> string or null depending on environment

## 6. C API

Header: `include/xon_api.h`

### 6.1 Parse and Eval
- `XonValue* xonify(const char* filename)`
- `XonValue* xonify_string(const char* xon_string)`
- `XonValue* xon_eval(const XonValue* value)`
- `void xon_free(XonValue* value)`

### 6.2 Type Access
- `XonType xon_get_type(const XonValue* value)`
- `xon_is_null/bool/number/string/object/list`
- `xon_get_bool`, `xon_get_number`, `xon_get_string`

### 6.3 Object/List Access
- `xon_object_get`, `xon_object_has`, `xon_object_size`
- `xon_object_key_at`, `xon_object_value_at`
- `xon_list_get`, `xon_list_size`

### 6.4 Serialization
- `char* xon_to_json(const XonValue* value, int pretty)`
- `char* xon_to_xon(const XonValue* value, int pretty)`
- `void xon_string_free(char* str)`

### 6.5 Logging
- `int xon_set_log_directory(const char* directory)`
- `void xon_set_log_level(XonLogLevel level)`
- `void xon_enable_stderr_logging(int enabled)`
- `void xon_shutdown_logging(void)`

### 6.6 Minimal C Example

```c
#include <stdio.h>
#include <xon_api.h>

int main(void) {
    XonValue* parsed = xonify("examples/config.xon");
    if (!parsed) return 1;

    XonValue* evaluated = xon_eval(parsed);
    if (!evaluated) {
        xon_free(parsed);
        return 1;
    }

    char* rendered = xon_to_xon(evaluated, 1);
    if (rendered) {
        puts(rendered);
        xon_string_free(rendered);
    }

    xon_free(evaluated);
    xon_free(parsed);
    return 0;
}
```

## 7. Node API and CLI

Package: `@xerxisfy/xon`

### 7.1 JS API

Exports from `index.js`:
- `xonify(path)`
- `xonifyString(input)`
- aliases: `parseFile`, `parseString`, `parse`
- `stringify(value, { indent })`

### 7.2 npm CLI

Entry point: `bin/xon`

Supported commands:
- `xon <file.xon>`
- `xon parse <file.xon>`
- `xon validate <file.xon>`
- `xon format <input.xon> [-o output.xon]`
- `xon convert <input.(xon|json)> <output.(json|xon)>`
- `xon eval <file.xon>`
- `xon build [input] [output]` (build-time JSON generation)

### 7.3 Node Build Notes

- Requires Node >= 18.
- Native addon built with `node-gyp` during install.
- C sources are compiled from package source list.

### 7.4 Build-Time Conversion for Frameworks

Important compatibility rule:
- Browsers/frameworks do not automatically parse `.xon` like `.json`.
- Xon must run in your build or backend runtime.

Recommended framework pattern:

```bash
# default convention
# config/app.xon -> src/generated/app-config.json
npx xon build

# project-specific mapping
npx xon build config src/generated
```

Optional zero-arg project convention:

Create `xon.config.json` in project root:

```json
{
  "build": {
    "input": "config",
    "output": "src/generated"
  }
}
```

Then developers can run:

```bash
npx xon build
```

Behavior rules:
- CLI args override config values.
- If config file is absent, fallback defaults apply.
- If config file is invalid JSON or invalid shape, build fails with a clear error.

Application code imports generated JSON:

```javascript
import appConfig from "./generated/app-config.json";
```

CI/build script integration example:

```json
{
  "scripts": {
    "xon:build": "xon build config src/generated",
    "build": "npm run xon:build && next build"
  }
}
```

What this guarantees:
- If `.xon` files pass `xon validate`, generated JSON output is parser-correct for standard JSON consumers.
- The generated `.json` files can be loaded by normal JSON paths in Node/web frameworks.

What this does not guarantee:
- Direct native `.xon` loading by browsers without a build/runtime integration step.

## 8. Python Binding

Module path: `bindings/python/xon.py`

Core wrapper behavior:
- Loads native library (`libxon.*`) via `ctypes`.
- Exposes parse and dump helpers.
- Can use `XON_LIB_PATH` override to locate native library.

Local test:
```bash
python3 tests/test_python.py
```

## 9. Build and Toolchain

### 9.1 Primary Build

```bash
./build.sh
```

What it does:
- Attempts parser regeneration via bundled `tools/lemon`.
- If unavailable for host architecture, attempts host-compiled Lemon (`build/lemon-host`).
- Falls back to checked-in parser sources when regeneration is unavailable.
- Builds `libxon` shared library, CLI binary, and example program.

Strict parser regeneration mode:
```bash
XON_REQUIRE_PARSER_GEN=1 ./build.sh
```

### 9.2 Tests

Core tests:
```bash
./scripts/run_tests.sh
```

Node release-focused tests:
```bash
npm run test:c
npm run test:node
npm run test:cli
```

### 9.3 Release Preflight

```bash
./scripts/release_check.sh
```

## 10. Playground, LSP, and VS Code

### 10.1 Playground (`play.ground/`)

Modes:
- Browser mode (WASM parse/eval)
- Optional sandbox API mode (`/api/eval`)

Typical flow:
```bash
cd play.ground
npm install
npm run build
npm start
```

### 10.2 Language Server (`xon-language-server/`)

Features:
- Validation diagnostics using `xon validate`
- Formatting using `xon format`

Requires `xon` CLI in PATH.

### 10.3 VS Code Extension (`vscode-xon/`)

Features:
- language registration for `.xon`
- syntax highlighting
- validate/format commands

Note:
- Current `vsce` toolchain expects Node 20+ for packaging.

## 11. Logging and Diagnostics

Default behavior:
- Logs written under `logs/` by CLI/runtime.

Environment variables:
- `XON_LOG_DIR`
- `XON_LOG_LEVEL` (`debug`, `info`, `warn`, `error`)
- `XON_LOG_STDERR` (`0` or `1`)

Diagnostics behavior:
- syntax errors include line context from parser callbacks.
- eval errors include explicit messages for arity/type/runtime failures.

## 12. Compatibility and Platform Notes

- Node package target: Node >= 18.
- Native build: C99 compiler required.
- Lemon binary in repo may be architecture-specific; build fallback exists.
- macOS runtime may require dynamic loader environment variables for manual C examples.

## 13. Security and Safety Notes

Current status:
- Parser and eval engine are suitable for trusted config usage.
- Sandbox-safe execution policy is not a complete security boundary by default.

Recommendations:
- Do not evaluate untrusted input in privileged production contexts without process isolation.
- For hosted evaluation, use worker isolation + hard timeouts + request size limits.
- Keep runtime options constrained if exposing eval over network APIs.

## 14. Known Risks and Technical Debt

- Grammar currently reports parser conflicts during Lemon generation; checked-in parser artifacts are used.
- Doc and package surfaces beyond core npm package (LSP, VS Code, playground deployment) are evolving and should be versioned independently.
- Subproject publish automation should be separated from core package prepublish checks to avoid toolchain coupling.

## 15. Development Workflow

Suggested day-to-day loop:
```bash
./build.sh
./scripts/run_tests.sh
npm run test:node
```

Before opening PR:
```bash
npm run prepublishOnly
./scripts/release_check.sh
```

## 16. Canonical Examples

### 16.1 Basic Config

```xon
{
  app_name: "XonApp",
  version: "1.0.0",
  debug: true,
  server: {
    host: "0.0.0.0",
    port: 8080,
    max_connections: 0xFF,
  },
}
```

### 16.2 Runtime Expressions

```xon
{
  let base = 10,
  let add = (a, b) => a + b + base,
  computed: add(2, 3),
  fallback: null ?? 42,
  status: if (computed > 10) "large" else "small",
}
```

### 16.3 CLI Conversion

```bash
xon convert examples/config.xon /tmp/config.json
xon convert /tmp/config.json /tmp/config-roundtrip.xon
```

## 17. Documentation Policy

Authoritative docs for this repo:
- `README.md` (entry overview)
- `docs.md` (long-lived technical reference)
- `release-docs.md` (release and operations runbook)

All other markdown documentation is intentionally removed to avoid drift.

## 18. Maintainer Contact

- Maintainer: Isaac Muliro
- Repository: https://github.com/xerxisfy/xon
- Issues: https://github.com/xerxisfy/xon/issues
- Discussions: https://github.com/xerxisfy/xon/discussions
- Email: xerxisfyágmail.com
