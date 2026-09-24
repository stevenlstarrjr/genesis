param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-spatial-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$scene = Get-Content (Join-Path $root 'assets/lighting/emissive-mesh-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
$source = [Convert]::FromBase64String(($scene.buffers[0].uri -split ',')[1])
$stream = [System.IO.MemoryStream]::new()
$stream.Write($source, 0, 108) # unchanged floor positions, normals, and indices
$writer = [System.IO.BinaryWriter]::new($stream)
foreach ($center in @(-3.0, 3.0)) {
    foreach ($vertex in @(@(-1.0,-1.0), @(1.0,-1.0), @(-1.0,1.0), @(1.0,1.0))) {
        $writer.Write([single]($center + $vertex[0]))
        $writer.Write([single]3.0)
        $writer.Write([single]$vertex[1])
    }
}
foreach ($i in 0..7) {
    $writer.Write([single]0.0); $writer.Write([single]-1.0); $writer.Write([single]0.0)
}
foreach ($index in @(0,1,2,1,3,2,4,5,6,5,7,6)) { $writer.Write([uint16]$index) }
$scene.buffers[0].byteLength = [int]$stream.Length
$scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($stream.ToArray())
$scene.bufferViews[3].byteOffset = 108; $scene.bufferViews[3].byteLength = 96
$scene.bufferViews[4].byteOffset = 204; $scene.bufferViews[4].byteLength = 96
$scene.bufferViews[5].byteOffset = 300; $scene.bufferViews[5].byteLength = 24
$scene.accessors[3].count = 8
$scene.accessors[3].min = @(-4.0, 3.0, -1.0)
$scene.accessors[3].max = @(4.0, 3.0, 1.0)
$scene.accessors[4].count = 8
$scene.accessors[5].count = 12
$scenePath = Join-Path $output 'two-panels.gltf'
$scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
$script = (Get-Content (Join-Path $root 'demos/emissive_mesh_lighting.py') -Raw).Replace(
    'assets/lighting/emissive-mesh-stage.gltf', $scenePath.Replace('\', '/'))
$scriptPath = Join-Path $output 'two-panels.py'
Set-Content -Encoding utf8 $scriptPath $script
$writer.Dispose(); $stream.Dispose()
& (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output 'two-panels')
if ($LASTEXITCODE -ne 0) { throw 'Spatial emissive regression render failed' }
$bitmap = [System.Drawing.Bitmap]::new((Join-Path $output 'two-panels.png'))
$left = $bitmap.GetPixel(400, 500).R
$middle = $bitmap.GetPixel(640, 500).R
$right = $bitmap.GetPixel(880, 500).R
$bitmap.Dispose()
Write-Host "Floor red values: left $left, midpoint $middle, right $right"
if ($left -le $middle + 15 -or $right -le $middle + 15) {
    throw 'Two panels did not create two separated floor-light pools'
}
