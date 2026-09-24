param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-strip-shadow-test'
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
$scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($bytes)
$scenePath = Join-Path $output 'strip.gltf'
$scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
$script = (Get-Content (Join-Path $root 'demos/emissive_mesh_shadows.py') -Raw).Replace(
    'assets/lighting/emissive-occlusion-stage.gltf', $scenePath.Replace('\', '/'))
$scriptPath = Join-Path $output 'strip.py'
Set-Content -Encoding utf8 $scriptPath $script
$writer.Dispose(); $stream.Dispose()
& (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output 'strip')
if ($LASTEXITCODE -ne 0) { throw 'Strip-shadow render failed' }
$bitmap = [System.Drawing.Bitmap]::new((Join-Path $output 'strip.png'))
$shadow = $bitmap.GetPixel(640, 470).R
$lit = $bitmap.GetPixel(400, 470).R
$bitmap.Dispose()
Write-Host "Strip floor red: shadow $shadow, lit $lit"
if ($lit -le $shadow + 15) { throw 'Strip did not cast a visible blocker shadow' }
