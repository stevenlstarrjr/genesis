param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/editor-quality-switch-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$stdout = Join-Path $output 'stdout.txt'
$stderr = Join-Path $output 'stderr.txt'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class EditorQualityWindow {
    public delegate bool WindowCallback(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
}
'@
$process = Start-Process -FilePath (Join-Path $root $Executable) `
    -ArgumentList @('--editor', (Join-Path $root 'examples/editor_project')) `
    -WorkingDirectory $root -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100 -and $window -eq [IntPtr]::Zero; ++$attempt) {
        $callback = [EditorQualityWindow+WindowCallback]{
            param($handle, $state)
            [uint32]$owner = 0
            [void][EditorQualityWindow]::GetWindowThreadProcessId($handle, [ref]$owner)
            if ($owner -eq $process.Id) { $script:window = $handle; return $false }
            return $true
        }
        [void][EditorQualityWindow]::EnumWindows($callback, [IntPtr]::Zero)
        if ($window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Editor window did not appear' }
    Start-Sleep -Milliseconds 1500
    foreach ($step in 1..3) {
        [void][EditorQualityWindow]::PostMessage($window, 0x100, [IntPtr]0x75, [IntPtr]0)
        [void][EditorQualityWindow]::PostMessage($window, 0x101, [IntPtr]0x75, [IntPtr]0)
        Start-Sleep -Milliseconds 650
    }
    [void][EditorQualityWindow]::PostMessage($window, 0x10, [IntPtr]0, [IntPtr]0)
    if (-not $process.WaitForExit(10000)) { throw 'Editor did not exit after window close' }
    $process.Refresh()
    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) { throw "Editor exited with code $($process.ExitCode): $(Get-Content $stderr -Raw)" }
    $switches = @(Get-Content $stdout | Select-String '^Genesis quality: (.+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    Write-Host "Editor quality sequence: $($switches -join ', ')"
    if (($switches -join ',') -ne 'high,low,medium') { throw 'Editor F6 did not switch all three live presets' }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
}
