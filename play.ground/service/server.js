"use strict";

const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const { spawn } = require("node:child_process");
const { performance } = require("node:perf_hooks");

const PLAYGROUND_ROOT = path.resolve(__dirname, "..");
const PROJECT_ROOT = path.resolve(__dirname, "..", "..");
const WORKER_PATH = path.resolve(__dirname, "eval_worker.js");

function readPositiveInt(name, fallback) {
    const raw = process.env[name];
    if (!raw) return fallback;
    const parsed = Number(raw);
    if (!Number.isFinite(parsed) || parsed <= 0) return fallback;
    return Math.floor(parsed);
}

const PORT = readPositiveInt("PORT", 8000);
const HOST = process.env.HOST || "127.0.0.1";
const MAX_BODY_BYTES = readPositiveInt("XON_SANDBOX_MAX_BODY_BYTES", 512 * 1024);
const MAX_SOURCE_BYTES = readPositiveInt("XON_SANDBOX_MAX_SOURCE_BYTES", 128 * 1024);
const REQUEST_TIMEOUT_MS = readPositiveInt("XON_SANDBOX_REQUEST_TIMEOUT_MS", 1200);
const HARD_TIMEOUT_MS = Math.min(REQUEST_TIMEOUT_MS + 400, 15000);

const MIME_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".wasm": "application/wasm",
    ".png": "image/png",
    ".svg": "image/svg+xml",
    ".txt": "text/plain; charset=utf-8",
};

const EVAL_DEFAULTS = {
    maxCallDepth: 256,
    maxCallCount: 20000,
    maxEvalSteps: 150000,
    timeoutMs: 600,
    maxResultNodes: 50000,
    maxAllocBytes: 16 * 1024 * 1024,
    watchdogPollSteps: 256,
    duplicateKeys: "error",
    typeMode: "dynamic",
};

const EVAL_LIMITS = {
    maxCallDepth: 1024,
    maxCallCount: 100000,
    maxEvalSteps: 500000,
    timeoutMs: Math.min(REQUEST_TIMEOUT_MS, 2000),
    maxResultNodes: 200000,
    maxAllocBytes: 64 * 1024 * 1024,
    watchdogPollSteps: 4096,
};

function sendJson(res, statusCode, payload) {
    const body = JSON.stringify(payload);
    res.writeHead(statusCode, {
        "Content-Type": "application/json; charset=utf-8",
        "Content-Length": Buffer.byteLength(body),
        "Cache-Control": "no-store",
    });
    res.end(body);
}

function sendText(res, statusCode, text) {
    res.writeHead(statusCode, {
        "Content-Type": "text/plain; charset=utf-8",
        "Content-Length": Buffer.byteLength(text),
        "Cache-Control": "no-store",
    });
    res.end(text);
}

function sanitizeEvalOptions(input) {
    const options = { ...EVAL_DEFAULTS };
    if (!input || typeof input !== "object" || Array.isArray(input)) {
        return options;
    }

    for (const key of Object.keys(EVAL_LIMITS)) {
        if (input[key] == null) continue;
        const parsed = Number(input[key]);
        if (!Number.isFinite(parsed) || parsed < 0) continue;
        options[key] = Math.min(Math.floor(parsed), EVAL_LIMITS[key]);
    }

    if (input.duplicateKeys === "first" || input.duplicateKeys === "last" || input.duplicateKeys === "error") {
        options.duplicateKeys = input.duplicateKeys;
    }
    if (input.typeMode === "dynamic" || input.typeMode === "strict" || input.typeMode === "strong") {
        options.typeMode = input.typeMode;
    }

    return options;
}

function readRequestBody(req, maxBytes) {
    return new Promise((resolve, reject) => {
        let body = "";
        let total = 0;

        req.setEncoding("utf8");
        req.on("data", (chunk) => {
            total += Buffer.byteLength(chunk);
            if (total > maxBytes) {
                reject(new Error("Request body too large"));
                req.destroy();
                return;
            }
            body += chunk;
        });
        req.on("end", () => resolve(body));
        req.on("error", reject);
    });
}

function runSandboxEval(code, options) {
    return new Promise((resolve, reject) => {
        const child = spawn(process.execPath, [WORKER_PATH], {
            cwd: PROJECT_ROOT,
            stdio: ["pipe", "pipe", "pipe"],
            env: {
                ...process.env,
                XON_SANDBOX_MAX_SOURCE_BYTES: String(MAX_SOURCE_BYTES),
            },
        });

        let stdout = "";
        let stderr = "";
        let timedOut = false;

        const killTimer = setTimeout(() => {
            timedOut = true;
            child.kill("SIGKILL");
        }, HARD_TIMEOUT_MS);

        child.stdout.setEncoding("utf8");
        child.stderr.setEncoding("utf8");
        child.stdout.on("data", (chunk) => {
            stdout += chunk;
        });
        child.stderr.on("data", (chunk) => {
            stderr += chunk;
        });

        child.on("error", (err) => {
            clearTimeout(killTimer);
            reject(err);
        });

        child.on("close", (codeValue) => {
            clearTimeout(killTimer);
            if (timedOut) {
                reject(new Error("Sandbox request timed out"));
                return;
            }
            if (!stdout.trim()) {
                reject(new Error(stderr.trim() || `Sandbox exited with code ${codeValue}`));
                return;
            }
            try {
                const parsed = JSON.parse(stdout);
                resolve(parsed);
            } catch (err) {
                reject(new Error(`Invalid sandbox response: ${err.message}`));
            }
        });

        child.stdin.end(
            JSON.stringify({
                code,
                options,
            }),
        );
    });
}

function toStaticPath(urlPathname) {
    let pathname = urlPathname;
    if (pathname === "/") pathname = "/index.html";
    const decoded = decodeURIComponent(pathname);
    const normalized = path.normalize(decoded).replace(/^(\.\.(\/|\\|$))+/, "");
    const resolved = path.resolve(PLAYGROUND_ROOT, `.${normalized}`);
    if (!resolved.startsWith(`${PLAYGROUND_ROOT}${path.sep}`) && resolved !== PLAYGROUND_ROOT) {
        return null;
    }
    return resolved;
}

function serveStatic(res, urlPathname) {
    const resolved = toStaticPath(urlPathname);
    if (!resolved) {
        sendText(res, 403, "Forbidden");
        return;
    }

    fs.stat(resolved, (statErr, statInfo) => {
        if (statErr || !statInfo.isFile()) {
            sendText(res, 404, "Not Found");
            return;
        }
        const ext = path.extname(resolved).toLowerCase();
        const contentType = MIME_TYPES[ext] || "application/octet-stream";
        fs.readFile(resolved, (readErr, content) => {
            if (readErr) {
                sendText(res, 500, "Failed to read static file");
                return;
            }
            res.writeHead(200, {
                "Content-Type": contentType,
                "Content-Length": content.length,
                "Cache-Control": ext === ".html" ? "no-cache" : "public, max-age=300",
            });
            res.end(content);
        });
    });
}

const server = http.createServer(async (req, res) => {
    const url = new URL(req.url || "/", "http://localhost");

    if (req.method === "GET" && url.pathname === "/api/healthz") {
        sendJson(res, 200, {
            ok: true,
            service: "xon-playground-sandbox",
            limits: {
                maxBodyBytes: MAX_BODY_BYTES,
                maxSourceBytes: MAX_SOURCE_BYTES,
                requestTimeoutMs: REQUEST_TIMEOUT_MS,
            },
        });
        return;
    }

    if (req.method === "POST" && url.pathname === "/api/eval") {
        const start = performance.now();
        try {
            const raw = await readRequestBody(req, MAX_BODY_BYTES);
            const payload = JSON.parse(raw || "{}");
            const code = payload.code;
            if (typeof code !== "string") {
                sendJson(res, 400, {
                    ok: false,
                    error: { message: "Request must include string field 'code'" },
                });
                return;
            }
            const sourceBytes = Buffer.byteLength(code, "utf8");
            if (sourceBytes > MAX_SOURCE_BYTES) {
                sendJson(res, 413, {
                    ok: false,
                    error: { message: `Input too large (${sourceBytes} bytes). Limit is ${MAX_SOURCE_BYTES} bytes.` },
                });
                return;
            }

            const options = sanitizeEvalOptions(payload.options);
            const workerResult = await runSandboxEval(code, options);
            const elapsedMs = Math.round(performance.now() - start);

            sendJson(res, 200, {
                ...workerResult,
                meta: {
                    durationMs: elapsedMs,
                    mode: "sandbox",
                    options,
                },
            });
        } catch (err) {
            const elapsedMs = Math.round(performance.now() - start);
            const status = /too large/i.test(err.message) ? 413 : 500;
            sendJson(res, status, {
                ok: false,
                error: {
                    message: err.message || "Sandbox evaluation failed",
                },
                meta: {
                    durationMs: elapsedMs,
                    mode: "sandbox",
                },
            });
        }
        return;
    }

    if (req.method === "GET") {
        serveStatic(res, url.pathname);
        return;
    }

    sendText(res, 405, "Method Not Allowed");
});

server.listen(PORT, HOST, () => {
    console.log(`[xon-playground] serving ${PLAYGROUND_ROOT}`);
    console.log(`[xon-playground] sandbox API enabled at http://${HOST}:${PORT}/api/eval`);
});
