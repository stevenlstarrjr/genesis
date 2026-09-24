param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-dual-origin-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$scene = Get-Content (Join-Path $root 'assets/lighting/emissive-occlusion-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
$bytes = [Convert]::FromBase64String(($scene.buffers[0].uri -split ',')[1])
$stream = [System.IO.MemoryStream]::new($bytes, $true)
$writer = [System.IO.BinaryWriter]::new($stream)
$stream.Position = 108
foreach ($vertex in @(@(-3.0,-0.5), @(3.0,-0.5), @(-3.0,0.5), @(3.0,0.5))) {
    $writer.Write([single]$vertex[0]); $writer.Write([single]3.0); $writer.Write([single]$vertex[1])
}
$scene.accessors[3].min = @(-3.0, 3.0, -0.5)
$scene.accessors[3].max = @(3.0, 3.0, 0.5)
# One source triangle stays one cluster even though its footprint is wide.
$scene.accessors[5].count = 3
$scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($bytes)
$scenePath = Join-Path $output 'wide-triangle.gltf'
$scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
$script = (Get-Content (Join-Path $root 'demos/emissive_mesh_shadows.py') -Raw).Replace(
    'assets/lighting/emissive-occlusion-stage.gltf', $scenePath.Replace('\', '/'))
$scriptPath = Join-Path $output 'wide-triangle.py'
Set-Content -Encoding utf8 $scriptPath $script
$writer.Dispose(); $stream.Dispose()
$renderOutput = & (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output 'wide-triangle') 2>&1
$renderOutput | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw 'Dual-origin render failed' }
$timings = ($renderOutput | Select-String 'draws=(\d+)' | Select-Object -Last 1)
if (-not $timings -or [int]$timings.Matches[0].Groups[1].Value -lt 75) {
    throw 'Wide emitter did not use both local shadow cubemaps'
}
$bitmap = [System.Drawing.Bitmap]::new((Join-Path $output 'wide-triangle.png'))
$shadow = $bitmap.GetPixel(640, 470).R
$lit = $bitmap.GetPixel(400, 470).R
$bitmap.Dispose()
Write-Host "Wide triangle floor red: shadow $shadow, lit $lit"
if ($lit -lt 55 -or $lit -le $shadow + 12) {
    throw 'Wide emitter did not preserve its lit side and blocker shadow'
}
