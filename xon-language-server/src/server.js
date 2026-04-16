#!/usr/bin/env node

const { spawnSync } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");
const {
  createConnection,
  TextDocuments,
  ProposedFeatures,
  DiagnosticSeverity,
  TextDocumentSyncKind,
} = require("vscode-languageserver/node");
const { TextDocument } = require("vscode-languageserver-textdocument");

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);
let hasConfigurationCapability = false;

function runXon(args, content) {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "xon-lsp-"));
  const tmpFile = path.join(tmpDir, "document.xon");
  fs.writeFileSync(tmpFile, content, "utf8");
  const fullArgs = args.map((arg) => (arg === "{file}" ? tmpFile : arg));
  const result = spawnSync("xon", fullArgs, { encoding: "utf8" });
  try {
    fs.unlinkSync(tmpFile);
    fs.rmdirSync(tmpDir);
  } catch (_) {
    // best-effort cleanup
  }
  return result;
}

function parseLine(stderrText) {
  const match = /line\s+(\d+)/i.exec(stderrText);
  if (!match) return 0;
  const line = Number(match[1]);
  return Number.isFinite(line) && line > 0 ? line - 1 : 0;
}

function validateTextDocument(textDocument) {
  const content = textDocument.getText();
  const result = runXon(["validate", "{file}"], content);
  const diagnostics = [];

  if (result.status !== 0) {
    const message = (result.stderr || result.stdout || "Invalid Xon").trim();
    diagnostics.push({
      severity: DiagnosticSeverity.Error,
      range: {
        start: { line: parseLine(message), character: 0 },
        end: { line: parseLine(message), character: 1 },
      },
      message,
      source: "xon-language-server",
    });
  }

  connection.sendDiagnostics({ uri: textDocument.uri, diagnostics });
}

connection.onInitialize((params) => {
  const capabilities = params.capabilities;
  hasConfigurationCapability = !!(
    capabilities.workspace && !!capabilities.workspace.configuration
  );

  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      documentFormattingProvider: true,
    },
  };
});

connection.onInitialized(() => {
  if (hasConfigurationCapability) {
    connection.client.register("workspace/didChangeConfiguration");
  }
});

documents.onDidOpen((event) => {
  validateTextDocument(event.document);
});

documents.onDidChangeContent((change) => {
  validateTextDocument(change.document);
});

connection.onDocumentFormatting((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const result = runXon(["format", "{file}"], doc.getText());
  if (result.status !== 0) return [];

  return [
    {
      range: {
        start: { line: 0, character: 0 },
        end: { line: Number.MAX_SAFE_INTEGER, character: Number.MAX_SAFE_INTEGER },
      },
      newText: result.stdout,
    },
  ];
});

documents.listen(connection);
connection.listen();
