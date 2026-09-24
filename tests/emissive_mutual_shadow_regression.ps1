param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-mutual-shadow-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$sourceScene = Get-Content (Join-Path $root 'assets/lighting/emissive-mesh-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
$sourceBuffer = [Convert]::FromBase64String(($sourceScene.buffers[0].uri -split ',')[1])
$bitmap = [System.Drawing.Bitmap]::new(2, 1)
$bitmap.SetPixel(0, 0, [System.Drawing.Color]::White)
$bitmap.SetPixel(1, 0, [System.Drawing.Color]::Black)
$bitmap.Save((Join-Path $output 'emission.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()

function Make-Scene([string]$name, [bool]$withOccluder) {
    $scene = Get-Content (Join-Path $root 'assets/lighting/emissive-mesh-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
    $stream = [System.IO.MemoryStream]::new()
    $stream.Write($sourceBuffer, 0, 108)
    $writer = [System.IO.BinaryWriter]::new($stream)
    foreach ($height in @(7.0, 2.0)) {
        foreach ($vertex in @(@(-1.0,-1.0), @(1.0,-1.0), @(-1.0,1.0), @(1.0,1.0))) {
            $writer.Write([single]$vertex[0]); $writer.Write([single]$height); $writer.Write([single]$vertex[1])
        }
    }
    foreach ($i in 0..7) {
        $writer.Write([single]0.0); $writer.Write([single]-1.0); $writer.Write([single]0.0)
    }
    $panelIndices = if ($withOccluder) { @(0,1,2,1,3,2,4,5,6,5,7,6) } else { @(0,1,2,1,3,2) }
    foreach ($index in $panelIndices) { $writer.Write([uint16]$index) }
    $uvOffset = [int]$stream.Length
    foreach ($i in 0..7) {
        $writer.Write([single]($(if ($i -lt 4) { 0.25 } else { 0.75 })))
        $writer.Write([single]0.5)
    }
    $scene.buffers[0].byteLength = [int]$stream.Length
    $scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($stream.ToArray())
    $scene.bufferViews[3].byteOffset = 108; $scene.bufferViews[3].byteLength = 96
    $scene.bufferViews[4].byteOffset = 204; $scene.bufferViews[4].byteLength = 96
    $scene.bufferViews[5].byteOffset = 300; $scene.bufferViews[5].byteLength = $panelIndices.Count * 2
    $scene.bufferViews += @{ buffer = 0; byteOffset = $uvOffset; byteLength = 64; target = 34962 }
    $scene.accessors[3].count = 8
    $scene.accessors[3].min = @(-1.0, 2.0, -1.0)
    $scene.accessors[3].max = @(1.0, 7.0, 1.0)
    $scene.accessors[4].count = 8
    $scene.accessors[5].count = $panelIndices.Count
    $scene.accessors += @{ bufferView = 6; componentType = 5126; count = 8; type = 'VEC2' }
    $scene.meshes[0].primitives[1].attributes.TEXCOORD_0 = 6
    $scene.images = @(@{ uri = 'emission.png' })
    $scene.textures = @(@{ source = 0 })
    $scene.materials[1].emissiveTexture = @{ index = 0 }
    $scenePath = Join-Path $output "$name.gltf"
    $scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
    $script = (Get-Content (Join-Path $root 'demos/emissive_mesh_lighting.py') -Raw).Replace(
        'assets/lighting/emissive-mesh-stage.gltf', $scenePath.Replace('\', '/'))
    $scriptPath = Join-Path $output "$name.py"
    Set-Content -Encoding utf8 $scriptPath $script
    $writer.Dispose(); $stream.Dispose()
    & (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output $name)
    if ($LASTEXITCODE -ne 0) { throw "Render failed for $name" }
}

Make-Scene 'clear' $false
Make-Scene 'occluded' $true
$clear = [System.Drawing.Bitmap]::new((Join-Path $output 'clear.png'))
$occluded = [System.Drawing.Bitmap]::new((Join-Path $output 'occluded.png'))
$clearRed = $clear.GetPixel(640, 510).R
$occludedRed = $occluded.GetPixel(640, 510).R
$clear.Dispose(); $occluded.Dispose()
Write-Host "Floor center red: clear $clearRed, occluded $occludedRed"
if ($clearRed -le $occludedRed + 10) {
    throw 'Panel sharing the emissive primitive did not shadow the floor'
}
