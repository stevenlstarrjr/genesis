param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-grazing-shadow-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$scene = Get-Content (Join-Path $root 'assets/lighting/emissive-occlusion-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
$bytes = [Convert]::FromBase64String(($scene.buffers[0].uri -split ',')[1])
$stream = [System.IO.MemoryStream]::new($bytes, $true)
$reader = [System.IO.BinaryReader]::new($stream, [System.Text.Encoding]::UTF8, $true)
$writer = [System.IO.BinaryWriter]::new($stream, [System.Text.Encoding]::UTF8, $true)
for ($vertex = 0; $vertex -lt 4; ++$vertex) {
    $stream.Position = 108 + $vertex * 12
    $x = $reader.ReadSingle()
    $stream.Position = 108 + $vertex * 12
    $writer.Write([single]($x - 4.0))
}
$scene.accessors[3].min = @(-5.0, 3.0, -1.0)
$scene.accessors[3].max = @(-3.0, 3.0, 1.0)
$scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($bytes)
$scenePath = Join-Path $output 'grazing-panel.gltf'
$scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
$script = (Get-Content (Join-Path $root 'demos/emissive_mesh_shadows.py') -Raw).Replace(
    'assets/lighting/emissive-occlusion-stage.gltf', $scenePath.Replace('\', '/'))
$scriptPath = Join-Path $output 'grazing-panel.py'
Set-Content -Encoding utf8 $scriptPath $script
$writer.Dispose(); $reader.Dispose(); $stream.Dispose()
& (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output 'grazing-panel')
if ($LASTEXITCODE -ne 0) { throw 'Grazing shadow render failed' }
$bitmap = [System.Drawing.Bitmap]::new((Join-Path $output 'grazing-panel.png'))
$deep = $bitmap.GetPixel(640, 480).R
$edge = $bitmap.GetPixel(696, 480).R
$lit = $bitmap.GetPixel(900, 480).R
$bitmap.Dispose()
Write-Host "Grazing floor red: deep $deep, edge $edge, lit $lit"
if ($deep -ge 40 -or $edge -lt 58 -or $edge -gt 85 -or $lit -le 140) {
    throw 'Grazing shadow lost its contact, transition, or lit floor'
}
