param(
    [string]$Executable = 'build/release/microsoft/genesis.exe',
    [int]$Steps = 3,
    [string]$SceneScript = 'demos/emissive_mesh_shadows.py'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/quality-switch-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$stdout = Join-Path $output 'stdout.txt'
$stderr = Join-Path $output 'stderr.txt'
$captureDirectory = Join-Path $root 'artifacts/screenshots'
$started = Get-Date
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class QualityTestWindow {
    public delegate bool WindowCallback(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr key, IntPtr flags);
}
'@
$process = Start-Process -FilePath (Join-Path $root $Executable) `
    -ArgumentList @('--script', (Join-Path $root $SceneScript)) `
    -WorkingDirectory $root -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100 -and $window -eq [IntPtr]::Zero; ++$attempt) {
        $callback = [QualityTestWindow+WindowCallback]{
            param($handle, $state)
            [uint32]$owner = 0
            [void][QualityTestWindow]::GetWindowThreadProcessId($handle, [ref]$owner)
            if ($owner -eq $process.Id) { $script:window = $handle; return $false }
            return $true
        }
        [void][QualityTestWindow]::EnumWindows($callback, [IntPtr]::Zero)
        if ($window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 100 }
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Renderer window did not appear' }
    Start-Sleep -Milliseconds 1500
    foreach ($step in 1..$Steps) {
        [void][QualityTestWindow]::PostMessage($window, 0x100, [IntPtr]0x75, [IntPtr]::Zero)
        [void][QualityTestWindow]::PostMessage($window, 0x101, [IntPtr]0x75, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 400
        [void][QualityTestWindow]::PostMessage($window, 0x100, [IntPtr]0x7B, [IntPtr]::Zero)
        [void][QualityTestWindow]::PostMessage($window, 0x101, [IntPtr]0x7B, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 1200
    }
    [void][QualityTestWindow]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(10000)) { throw 'Renderer did not exit after window close' }
    $process.Refresh()
    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) { throw "Renderer exited with code $($process.ExitCode): $(Get-Content $stderr -Raw)" }
    $switches = @(Get-Content $stdout | Select-String '^Genesis quality: (.+)$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    Write-Host "Live quality sequence: $($switches -join ', ')"
    $expected = @('high', 'low', 'medium')[0..($Steps - 1)]
    if (($switches -join ',') -ne ($expected -join ',')) { throw 'F6 did not cycle the quality presets' }
    $captures = @(Get-ChildItem $captureDirectory -Filter 'genesis-*.png' |
        Where-Object { $_.LastWriteTime -ge $started } | Sort-Object LastWriteTime)
    if ($captures.Count -lt $Steps) { throw "Only $($captures.Count) of $Steps switched-preset screenshots were saved" }
    Add-Type -AssemblyName System.Drawing
    foreach ($capture in $captures | Select-Object -Last $Steps) {
        $bitmap = [System.Drawing.Bitmap]::new($capture.FullName)
        $lit = $bitmap.GetPixel(400, 470).R
        $bitmap.Dispose()
        if ($lit -lt 20) { throw "Switched-preset scene was dark or missing: $($capture.FullName)" }
    }
    Write-Host "Verified $Steps rendered screenshots after quality switches"
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
}
