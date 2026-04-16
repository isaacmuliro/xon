"use strict";

const path = require("node:path");

const PROJECT_ROOT = path.resolve(__dirname, "..", "..");
const MAX_SOURCE_BYTES = (() => {
    const raw = process.env.XON_SANDBOX_MAX_SOURCE_BYTES;
    const parsed = Number(raw);
    if (!Number.isFinite(parsed) || parsed <= 0) return 128 * 1024;
    return Math.floor(parsed);
})();

function writeResult(payload) {
    process.stdout.write(JSON.stringify(payload));
}

function readStdin() {
    return new Promise((resolve, reject) => {
        let body = "";
        process.stdin.setEncoding("utf8");
        process.stdin.on("data", (chunk) => {
            body += chunk;
        });
        process.stdin.on("end", () => resolve(body));
        process.stdin.on("error", reject);
    });
}

function blockedBuiltins(input) {
    return /\b(?:include|import)\s*\(/.test(input);
}

async function main() {
    let xon;
    try {
        process.chdir(PROJECT_ROOT);
        xon = require(path.join(PROJECT_ROOT, "index.js"));
    } catch (err) {
        writeResult({
            ok: false,
            error: {
                message: `Failed to load Xon runtime: ${err.message}`,
            },
        });
        process.exit(0);
        return;
    }

    try {
        const raw = await readStdin();
        const payload = JSON.parse(raw || "{}");
        const code = payload.code;
        const options = payload.options;

        if (typeof code !== "string") {
            writeResult({
                ok: false,
                error: { message: "Sandbox payload is missing string field 'code'" },
            });
            process.exit(0);
            return;
        }

        const sourceBytes = Buffer.byteLength(code, "utf8");
        if (sourceBytes > MAX_SOURCE_BYTES) {
            writeResult({
                ok: false,
                error: { message: `Input too large (${sourceBytes} bytes).` },
            });
            process.exit(0);
            return;
        }

        if (blockedBuiltins(code)) {
            writeResult({
                ok: false,
                error: { message: "Sandbox blocks include()/import() calls for file-system safety." },
            });
            process.exit(0);
            return;
        }

        const evaluated = xon.evalString(code, options);
        writeResult({
            ok: true,
            result: evaluated,
            renderedXon: xon.stringify(evaluated, { indent: 2 }),
        });
        process.exit(0);
    } catch (err) {
        writeResult({
            ok: false,
            error: {
                message: err.message || "Evaluation failed",
                stack: err.stack || "",
            },
        });
        process.exit(0);
    }
}

main();
