import * as path from 'path';
import * as vscode from 'vscode';
import { 
    LanguageClient, 
    LanguageClientOptions, 
    ServerOptions, 
    Executable 
} from 'vscode-languageclient/node';
import * as fs from 'fs';

let client: LanguageClient;
let fennecTerminal: vscode.Terminal | undefined;
let outputChannel: vscode.OutputChannel;

/**
 * Checks if a candidate binary exists based on the current platform.
 */
function findExistingBinary(basePath: string): string | undefined {
    const isWin = process.platform === 'win32';
    const normalizedPath = path.normalize(basePath);

    if (isWin && !path.extname(normalizedPath)) {
        const exePath = `${normalizedPath}.exe`;
        if (fs.existsSync(exePath)) {
            try {
                if (fs.statSync(exePath).isFile()) {
                    return exePath;
                }
            } catch {
                // Ignore permission or file access errors
            }
        }
    }

    if (fs.existsSync(normalizedPath)) {
        try {
            if (fs.statSync(normalizedPath).isFile()) {
                return normalizedPath;
            }
        } catch {
            // Ignore access errors
        }
    }

    return undefined;
}

function resolveServerPath(context: vscode.ExtensionContext): string | undefined {
    const rawCandidates: string[] = [];
    const binaryName = process.platform === 'win32' ? 'fennec.exe' : 'fennec';

    const customPath = vscode.workspace.getConfiguration('fennec').get<string>('lsp.path');
    if (customPath && customPath.trim() !== '') {
        rawCandidates.push(customPath);
    }

    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
        const rootPath = workspaceFolders[0].uri.fsPath;
        
        rawCandidates.push(path.join(rootPath, 'build', binaryName));
        rawCandidates.push(path.join(rootPath, 'build', 'bin', binaryName));
        rawCandidates.push(path.join(rootPath, 'build', 'Debug', binaryName));
        rawCandidates.push(path.join(rootPath, 'build', 'Debug', 'bin', binaryName));
        rawCandidates.push(path.join(rootPath, 'build', 'Release', binaryName));
        rawCandidates.push(path.join(rootPath, 'build', 'Release', 'bin', binaryName));
        
        rawCandidates.push(path.join(rootPath, 'fennec', 'build', binaryName));
        rawCandidates.push(path.join(rootPath, 'fennec', 'build', 'bin', binaryName));
    }

    const extBase = context.extensionPath;
    rawCandidates.push(path.resolve(extBase, '..', '..', 'build', binaryName));
    rawCandidates.push(path.resolve(extBase, '..', '..', 'build', 'bin', binaryName));
    rawCandidates.push(path.resolve(extBase, '..', '..', 'build', 'Debug', binaryName));
    rawCandidates.push(path.resolve(extBase, '..', '..', 'build', 'Debug', 'bin', binaryName));
    rawCandidates.push(path.resolve(extBase, '..', '..', 'build', 'Release', binaryName));
    rawCandidates.push(path.resolve(extBase, '..', '..', 'build', 'Release', 'bin', binaryName));
    rawCandidates.push(path.resolve(extBase, '..', 'fennec', 'build', binaryName));
    rawCandidates.push(path.resolve(extBase, '..', 'fennec', 'build', 'bin', binaryName));

    for (const candidate of rawCandidates) {
        const resolved = findExistingBinary(candidate);
        if (resolved) {
            return resolved;
        }
    }

    return undefined;
}

/**
 * Tests existing terminals or creates a suitable terminal for execution.
 */
function getOrCreateTerminal(): vscode.Terminal {
    // 1. Reuse existing valid terminal instance if active
    if (fennecTerminal && fennecTerminal.exitStatus === undefined) {
        return fennecTerminal;
    }

    // 2. Iterate existing terminals to find a reusable named terminal
    const existingTerminals = vscode.window.terminals;
    for (const term of existingTerminals) {
        if (term.name === 'Fennec Build' && term.exitStatus === undefined) {
            fennecTerminal = term;
            return fennecTerminal;
        }
    }

    // 3. Fallback: Create a new terminal with adaptive shell detection
    const isWin = process.platform === 'win32';
    if (isWin) {
        // Test system for preferred shells
        const powershellPath = 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe';
        const cmdPath = 'C:\\Windows\\System32\\cmd.exe';

        if (fs.existsSync(powershellPath)) {
            fennecTerminal = vscode.window.createTerminal({
                name: 'Fennec Build',
                shellPath: powershellPath
            });
        } else if (fs.existsSync(cmdPath)) {
            fennecTerminal = vscode.window.createTerminal({
                name: 'Fennec Build',
                shellPath: cmdPath
            });
        } else {
            fennecTerminal = vscode.window.createTerminal('Fennec Build');
        }
    } else {
        fennecTerminal = vscode.window.createTerminal('Fennec Build');
    }

    return fennecTerminal;
}

/**
 * Formats terminal commands safely based on platform.
 */
function buildTerminalCommand(binaryPath: string, args: string): string {
    const isWin = process.platform === 'win32';
    
    if (isWin) {
        return `& "${binaryPath}" ${args}`;
    }
    return `"${binaryPath}" ${args}`;
}

/**
 * Resolves the primary workspace root folder path.
 */
function getWorkspaceFolder(): string | undefined {
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
        return workspaceFolders[0].uri.fsPath;
    }
    return undefined;
}

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('Vulpine Language Server');
    context.subscriptions.push(outputChannel);

    const serverPath = resolveServerPath(context);

    if (!serverPath) {
        outputChannel.appendLine('[Error] Fennec LSP binary could not be resolved in build/ folders.');
        vscode.window.showErrorMessage(
            `Fennec LSP binary not found. Please ensure it is built in 'build/' or set 'fennec.lsp.path' in settings.`
        );
    } else {
        outputChannel.appendLine(`[Fennec] Resolved LSP Executable Path: ${serverPath}`);

        const binaryDir = path.dirname(serverPath);
        const pathEnvSeparator = process.platform === 'win32' ? ';' : ':';

        // Prepend binary directory to PATH to fix missing DLL dependencies on launch
        const augmentedPath = `${binaryDir}${pathEnvSeparator}${process.env.PATH || ''}`;

        const serverExecutable: Executable = {
            command: serverPath,
            args: ['--lsp'],
            options: {
                env: {
                    ...process.env,
                    PATH: augmentedPath
                },
                cwd: binaryDir
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
            },
            outputChannel: outputChannel,
            traceOutputChannel: outputChannel
        };

        client = new LanguageClient(
            'vulpineToolkit',
            'Vulpine Toolkit Language Server',
            serverOptions,
            clientOptions
        );

        client.start().catch((err) => {
            outputChannel.appendLine(`[LSP Client Error] Failed to start LSP client: ${err}`);
            vscode.window.showErrorMessage(`Failed to launch Fennec LSP server: ${err.message || err}`);
        });
    }

    const defaultBinary = process.platform === 'win32' ? 'fennec.exe' : 'fennec';

    // Command 1: Fennec: Build Project
    const buildCmd = vscode.commands.registerCommand('fennec.build', async () => {
        const activeEditor = vscode.window.activeTextEditor;
        if (!activeEditor) {
            vscode.window.showWarningMessage('No active file to build.');
            return;
        }

        const workspaceFolder = getWorkspaceFolder();
        if (!workspaceFolder) {
            vscode.window.showErrorMessage('No workspace folder open.');
            return;
        }

        const filePath = activeEditor.document.uri.fsPath;
        const fileParsed = path.parse(filePath);
        const binaryPath = serverPath || defaultBinary;

        const outputExt = process.platform === 'win32' ? '.exe' : '';
        const outputPath = path.join(workspaceFolder, `${fileParsed.name}${outputExt}`);

        const terminal = getOrCreateTerminal();
        terminal.show();

        const cmd = buildTerminalCommand(binaryPath, `-i "${filePath}" -o "${outputPath}"`);
        terminal.sendText(cmd);
    });

    // Command 2: Fennec: Build & Run Project
    const buildAndRunCmd = vscode.commands.registerCommand('fennec.build.run', async () => {
        const activeEditor = vscode.window.activeTextEditor;
        if (!activeEditor) {
            vscode.window.showWarningMessage('No active file to build and run.');
            return;
        }

        const workspaceFolder = getWorkspaceFolder();
        if (!workspaceFolder) {
            vscode.window.showErrorMessage('No workspace folder open.');
            return;
        }

        const filePath = activeEditor.document.uri.fsPath;
        const fileParsed = path.parse(filePath);
        const binaryPath = serverPath || defaultBinary;

        const outputExt = process.platform === 'win32' ? '.exe' : '';
        const targetExe = path.join(workspaceFolder, `${fileParsed.name}${outputExt}`);

        const terminal = getOrCreateTerminal();
        terminal.show();

        const buildStep = buildTerminalCommand(binaryPath, `-i "${filePath}" -o "${targetExe}"`);

        if (process.platform === 'win32') {
            terminal.sendText(`${buildStep} ; if ($?) { & "${targetExe}" }`);
        } else {
            terminal.sendText(`${buildStep} && "${targetExe}"`);
        }
    });

    // Command 3: Vulpine: Optimize Selected Code
    const optimizeCmd = vscode.commands.registerCommand('vulpine.optimizer.run', async () => {
        const editor = vscode.window.activeTextEditor;
        if (!editor) {
            vscode.window.showWarningMessage('No active editor open to optimize.');
            return;
        }

        const selection = editor.selection;
        const selectedText = editor.document.getText(selection);

        if (!selectedText) {
            vscode.window.showInformationMessage('Please select code to optimize.');
            return;
        }

        vscode.window.showInformationMessage('Vulpine Optimizer: Processing selected block...');
    });

    context.subscriptions.push(buildCmd, buildAndRunCmd, optimizeCmd);
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}