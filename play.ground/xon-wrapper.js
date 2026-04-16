class XonParser {
    constructor(module) {
        this.module = module;
        this.api = {
            xonifyString: module.cwrap("xonify_string", "number", ["string"]),
            xonEval: module.cwrap("xon_eval", "number", ["number"]),
            xonToJson: module.cwrap("xon_to_json", "number", ["number", "number"]),
            xonToXon: module.cwrap("xon_to_xon", "number", ["number", "number"]),
            xonFree: module.cwrap("xon_free", null, ["number"]),
            xonStringFree: module.cwrap("xon_string_free", null, ["number"]),
            xonGetLastError: module.cwrap("xon_get_last_error", "string", []),
            xonGetLastErrorStack: module.cwrap("xon_get_last_error_stack", "string", []),
        };
    }

    _readStringAndFree(ptr) {
        if (!ptr) return "";
        const value = this.module.UTF8ToString(ptr);
        this.api.xonStringFree(ptr);
        return value;
    }

    _runtimeError(prefix) {
        const msg = this.api.xonGetLastError() || prefix;
        const stack = this.api.xonGetLastErrorStack() || "";
        return stack ? `${msg}\n${stack}` : msg;
    }

    _parsePointer(code) {
        const ptr = this.api.xonifyString(code);
        if (!ptr) {
            throw new Error(this._runtimeError("Failed to parse Xon input."));
        }
        return ptr;
    }

    _jsonFromPointer(ptr, pretty = true) {
        const jsonPtr = this.api.xonToJson(ptr, pretty ? 1 : 0);
        if (!jsonPtr) {
            throw new Error(this._runtimeError("Failed to serialize value to JSON."));
        }
        const jsonText = this._readStringAndFree(jsonPtr);
        return JSON.parse(jsonText);
    }

    parse(code) {
        let parsedPtr = 0;
        try {
            parsedPtr = this._parsePointer(code);
            const ast = this._jsonFromPointer(parsedPtr, true);
            return { success: true, ast, error: null };
        } catch (error) {
            return { success: false, ast: null, error: error.message };
        } finally {
            if (parsedPtr) this.api.xonFree(parsedPtr);
        }
    }

    evaluate(code) {
        let parsedPtr = 0;
        let evalPtr = 0;
        try {
            parsedPtr = this._parsePointer(code);
            evalPtr = this.api.xonEval(parsedPtr);
            if (!evalPtr) {
                throw new Error(this._runtimeError("Evaluation failed."));
            }
            const result = this._jsonFromPointer(evalPtr, true);
            return { success: true, result, error: null };
        } catch (error) {
            return { success: false, result: null, error: error.message };
        } finally {
            if (evalPtr) this.api.xonFree(evalPtr);
            if (parsedPtr) this.api.xonFree(parsedPtr);
        }
    }

    formatSource(code, pretty = true) {
        let parsedPtr = 0;
        try {
            parsedPtr = this._parsePointer(code);
            const xonPtr = this.api.xonToXon(parsedPtr, pretty ? 1 : 0);
            if (!xonPtr) {
                throw new Error(this._runtimeError("Failed to format Xon input."));
            }
            return { success: true, formatted: this._readStringAndFree(xonPtr), error: null };
        } catch (error) {
            return { success: false, formatted: "", error: error.message };
        } finally {
            if (parsedPtr) this.api.xonFree(parsedPtr);
        }
    }

    astToJSON(ast) {
        return ast;
    }

    jsonToAst(value) {
        return value;
    }

    formatAST(ast) {
        return this.formatXon(ast, 4);
    }

    formatXon(value, indentSize = 4, depth = 0) {
        const pad = " ".repeat(indentSize * depth);
        const childPad = " ".repeat(indentSize * (depth + 1));
        const isIdentifier = (key) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(key);

        if (value === null || value === undefined) return "null";
        if (typeof value === "boolean") return value ? "true" : "false";
        if (typeof value === "number") return Number.isFinite(value) ? String(value) : "null";
        if (typeof value === "string") return JSON.stringify(value);

        if (Array.isArray(value)) {
            if (value.length === 0) return "[]";
            return `[\n${value.map((item) => `${childPad}${this.formatXon(item, indentSize, depth + 1)}`).join(",\n")}\n${pad}]`;
        }

        if (typeof value === "object") {
            const entries = Object.entries(value);
            if (entries.length === 0) return "{}";
            return `{\n${entries
                .map(([key, item]) => {
                    const renderedKey = isIdentifier(key) ? key : JSON.stringify(key);
                    return `${childPad}${renderedKey}: ${this.formatXon(item, indentSize, depth + 1)}`;
                })
                .join(",\n")}\n${pad}}`;
        }

        return "null";
    }
}
