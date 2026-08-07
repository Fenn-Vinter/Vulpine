import * as path from 'path';
import * as vscode from 'vscode';
import { 
    LanguageClient, 
    LanguageClientOptions,
    ServerOptions, 
    Executable,
    TextDocumentSyncKind
} from 'vscode-languageclient/node';
import * as fs from 'fs';

let client: LanguageClient;

function resolveServerPath(context: vscode.ExtensionContext): string | undefined {
    const candidates: string[] = [];

    const customPath = vscode.workspace.getConfiguration('fennec').get<string>('lsp.path');
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

export function activate(context: vscode.ExtensionContext) {
    const serverPath = resolveServerPath(context);

    if (!serverPath) {
        vscode.window.showErrorMessage(
            `Fennec LSP binary not found. Expected at '/home/fenn/Desktop/Vulpine/build/fennec'. Ensure it is built or set 'fennec.lsp.path' in settings.`
        );
        return;
    }

    console.log(`[Fennec] Resolved LSP Path: ${serverPath}`);

    const serverExecutable: Executable = {
        command: serverPath,
        args: ['--lsp'],
        options: {
            env: process.env
        }
    };

    const serverOptions: ServerOptions = {
        run: serverExecutable,
        debug: serverExecutable
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'vulpine' }
        ],
        synchronize: {
            configurationSection: 'fennec'
        }
    };

    client = new LanguageClient(
        'vulpineToolkit',
        'Vulpine Toolkit Language Server',
        serverOptions,
        clientOptions
    );

    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}