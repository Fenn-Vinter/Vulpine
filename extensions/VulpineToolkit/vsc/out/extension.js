"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const path = require("path");
const vscode = require("vscode");
const node_1 = require("vscode-languageclient/node");
const fs = require("fs");
let client;
function resolveServerPath(context) {
    const candidates = [];
    const customPath = vscode.workspace.getConfiguration('fennec').get('lsp.path');
    if (customPath && customPath.trim() !== '') {
        candidates.push(customPath);
    }
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
        const rootPath = workspaceFolders[0].uri.fsPath;
        candidates.push(path.join(rootPath, 'build', 'fennec'));
        candidates.push(path.join(rootPath, 'fennec', 'build', 'fennec'));
    }
    candidates.push(path.resolve(context.extensionPath, '..', '..', 'build', 'fennec'));
    candidates.push(path.resolve(context.extensionPath, '..', 'fennec', 'build', 'fennec'));
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }
    return undefined;
}
function activate(context) {
    const serverPath = resolveServerPath(context);
    if (!serverPath) {
        vscode.window.showErrorMessage(`Fennec LSP binary not found. Expected at '/home/fenn/Desktop/Vulpine/build/fennec'. Ensure it is built or set 'fennec.lsp.path' in settings.`);
        return;
    }
    console.log(`[Fennec] Resolved LSP Path: ${serverPath}`);
    const serverExecutable = {
        command: serverPath,
        args: ['--lsp'],
        options: {
            env: process.env
        }
    };
    const serverOptions = {
        run: serverExecutable,
        debug: serverExecutable
    };
    const clientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'vulpine' }
        ],
        synchronize: {
            configurationSection: 'fennec'
        }
    };
    client = new node_1.LanguageClient('vulpineToolkit', 'Vulpine Toolkit Language Server', serverOptions, clientOptions);
    client.start();
}
function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
//# sourceMappingURL=extension.js.map