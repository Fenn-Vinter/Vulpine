# setup-dev-link.ps1
# Run this script to link the extension to VS Code on any Windows machine

$ProjectDir = $PSScriptRoot
$ExtensionName = (Get-Content "$ProjectDir\package.json" | ConvertFrom-Json).name
$LinkPath = Join-Path "$env:USERPROFILE\.vscode\extensions" $ExtensionName

Write-Host "Setting up dev link for '$ExtensionName'..." -ForegroundColor Cyan

# Remove existing link/directory if present
if (Test-Path $LinkPath) {
    Write-Host "Removing existing extension folder/link at target..." -ForegroundColor Yellow
    Remove-Item -Path $LinkPath -Recurse -Force
}

# Create symbolic link (falls back to Junction if symlink fails due to permissions)
try {
    New-Item -ItemType SymbolicLink -Path $LinkPath -Value $ProjectDir -ErrorAction Stop | Out-Null
    Write-Host "Successfully created Symbolic Link!" -ForegroundColor Green
} catch {
    Write-Host "Symbolic link creation failed (requires Admin/Dev Mode). Falling back to Directory Junction..." -ForegroundColor Yellow
    New-Item -ItemType Junction -Path $LinkPath -Value $ProjectDir | Out-Null
    Write-Host "Successfully created Directory Junction!" -ForegroundColor Green
}

Write-Host "`nExtension linked successfully to $LinkPath" -ForegroundColor Green
Write-Host "Reload VS Code (Ctrl+Shift+P -> 'Developer: Reload Window') to apply changes after building." -ForegroundColor Cyan