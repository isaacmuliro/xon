const { execFile } = require("child_process");
const vscode = require("vscode");

function runXon(args, cwd) {
  return new Promise((resolve, reject) => {
    execFile("xon", args, { cwd }, (error, stdout, stderr) => {
      if (error) {
        reject(new Error((stderr || stdout || error.message).trim()));
        return;
      }
      resolve(stdout.trim());
    });
  });
}

function activeXonEditor() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "xon") return null;
  return editor;
}

function workspaceCwd() {
  const folder = vscode.workspace.workspaceFolders?.[0];
  return folder ? folder.uri.fsPath : process.cwd();
}

async function validateCurrentFile() {
  const editor = activeXonEditor();
  if (!editor) {
    vscode.window.showWarningMessage("Open a .xon file to validate.");
    return;
  }

  if (editor.document.isDirty) {
    await editor.document.save();
  }

  try {
    const output = await runXon(["validate", editor.document.fileName], workspaceCwd());
    vscode.window.showInformationMessage(output || "Xon file is valid.");
  } catch (err) {
    vscode.window.showErrorMessage(`Xon validation failed: ${err.message}`);
  }
}

async function formatCurrentFile() {
  const editor = activeXonEditor();
  if (!editor) {
    vscode.window.showWarningMessage("Open a .xon file to format.");
    return;
  }

  if (editor.document.isDirty) {
    await editor.document.save();
  }

  try {
    const formatted = await runXon(["format", editor.document.fileName], workspaceCwd());
    const edit = new vscode.WorkspaceEdit();
    const full = new vscode.Range(
      editor.document.positionAt(0),
      editor.document.positionAt(editor.document.getText().length)
    );
    edit.replace(editor.document.uri, full, formatted + "\n");
    await vscode.workspace.applyEdit(edit);
    await editor.document.save();
    vscode.window.showInformationMessage("Xon formatting complete.");
  } catch (err) {
    vscode.window.showErrorMessage(`Xon formatting failed: ${err.message}`);
  }
}

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand("xon.validate", validateCurrentFile),
    vscode.commands.registerCommand("xon.format", formatCurrentFile)
  );
}

function deactivate() {}

module.exports = {
  activate,
  deactivate
};
