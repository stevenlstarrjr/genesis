param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-shared-shadow-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$scene = Get-Content (Join-Path $root 'assets/lighting/emissive-occlusion-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
$source = [Convert]::FromBase64String(($scene.buffers[0].uri -split ',')[1])
$stream = [System.IO.MemoryStream]::new()
$stream.Write($source, 0, 108) # Floor geometry stays unchanged.
$writer = [System.IO.BinaryWriter]::new($stream)
foreach ($center in @(-2.0, 0.0, 2.0)) {
    foreach ($vertex in @(@(-0.75,-0.5), @(0.75,-0.5), @(-0.75,0.5), @(0.75,0.5))) {
        $writer.Write([single]($center + $vertex[0]))
        $writer.Write([single]3.0)
        $writer.Write([single]$vertex[1])
    }
}
foreach ($i in 0..11) {
    $writer.Write([single]0.0); $writer.Write([single]-1.0); $writer.Write([single]0.0)
}
foreach ($base in @(0,4,8)) {
    foreach ($index in @(0,1,2,1,3,2)) { $writer.Write([uint16]($base + $index)) }
}
# Move the occluder under the third panel, which falls outside the two slots.
$box = [byte[]]::new(540)
[Array]::Copy($source, 216, $box, 0, 540)
$boxStream = [System.IO.MemoryStream]::new($box, $true)
$boxWriter = [System.IO.BinaryWriter]::new($boxStream)
$boxReader = [System.IO.BinaryReader]::new($boxStream, [System.Text.Encoding]::UTF8, $true)
for ($vertex = 0; $vertex -lt 20; ++$vertex) {
    $boxStream.Position = $vertex * 12
    $x = $boxReader.ReadSingle()
    $boxStream.Position = $vertex * 12
    $boxWriter.Write([single]($x + 2.0))
}
$stream.Write($box, 0, $box.Length)
$scene.buffers[0].byteLength = [int]$stream.Length
$scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($stream.ToArray())
$scene.bufferViews[3].byteOffset = 108; $scene.bufferViews[3].byteLength = 144
$scene.bufferViews[4].byteOffset = 252; $scene.bufferViews[4].byteLength = 144
$scene.bufferViews[5].byteOffset = 396; $scene.bufferViews[5].byteLength = 36
$scene.bufferViews[6].byteOffset = 432
$scene.bufferViews[7].byteOffset = 672
$scene.bufferViews[8].byteOffset = 912
$scene.accessors[3].count = 12
$scene.accessors[3].min = @(-2.75, 3.0, -0.5)
$scene.accessors[3].max = @(2.75, 3.0, 0.5)
$scene.accessors[4].count = 12
$scene.accessors[5].count = 18
$scene.accessors[6].min = @(1.3, 0.0, -0.7)
$scene.accessors[6].max = @(2.7, 1.6, 0.7)
$scenePath = Join-Path $output 'three-panels.gltf'
$scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
$script = (Get-Content (Join-Path $root 'demos/emissive_mesh_shadows.py') -Raw).Replace(
    'assets/lighting/emissive-occlusion-stage.gltf', $scenePath.Replace('\', '/'))
$scriptPath = Join-Path $output 'three-panels.py'
Set-Content -Encoding utf8 $scriptPath $script
$writer.Dispose(); $boxWriter.Dispose(); $boxReader.Dispose(); $boxStream.Dispose(); $stream.Dispose()
& (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output 'three-panels')
if ($LASTEXITCODE -ne 0) { throw 'Three-panel shadow render failed' }
$bitmap = [System.Drawing.Bitmap]::new((Join-Path $output 'three-panels.png'))
$shadow = $bitmap.GetPixel(456, 482).R
$lit = $bitmap.GetPixel(560, 482).R
Write-Host "Shared-capture floor red: shadow $shadow, lit $lit"
$bitmap.Dispose()
if ($shadow -ge 24 -or $lit -le 120) {
    throw 'The third emissive cluster leaked through its box shadow'
}
