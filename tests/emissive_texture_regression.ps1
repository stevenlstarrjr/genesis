param([string]$Executable = 'build/release/microsoft/genesis.exe')

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-texture-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = Get-Content (Join-Path $root 'assets/lighting/emissive-mesh-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
$baseBuffer = [Convert]::FromBase64String(($source.buffers[0].uri -split ',')[1])

function Make-Scene([string]$name, [System.Drawing.Color]$color) {
    $scene = Get-Content (Join-Path $root 'assets/lighting/emissive-mesh-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
    $stream = [System.IO.MemoryStream]::new()
    $stream.Write($baseBuffer, 0, $baseBuffer.Length)
    $writer = [System.IO.BinaryWriter]::new($stream)
    foreach ($uv in @(@(0.0, 0.0), @(1.0, 0.0), @(0.0, 1.0), @(1.0, 1.0))) {
        $writer.Write([single]$uv[0]); $writer.Write([single]$uv[1])
    }
    $scene.buffers[0].byteLength = [int]$stream.Length
    $scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($stream.ToArray())
    $scene.bufferViews += @{ buffer = 0; byteOffset = $baseBuffer.Length; byteLength = 32; target = 34962 }
    $scene.accessors += @{ bufferView = 6; componentType = 5126; count = 4; type = 'VEC2' }
    $scene.meshes[0].primitives[1].attributes.TEXCOORD_0 = 6
    $bitmap = [System.Drawing.Bitmap]::new(2, 2)
    for ($y = 0; $y -lt 2; $y++) { for ($x = 0; $x -lt 2; $x++) { $bitmap.SetPixel($x, $y, $color) } }
    $bitmap.Save((Join-Path $output "$name-texture.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $scene.images = @(@{ uri = "$name-texture.png" })
    $scene.textures = @(@{ source = 0 })
    $scene.materials[1].emissiveFactor = @(1.0, 1.0, 1.0)
    $scene.materials[1].emissiveTexture = @{ index = 0 }
    $scenePath = Join-Path $output "$name.gltf"
    $scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
    $script = (Get-Content (Join-Path $root 'demos/emissive_mesh_lighting.py') -Raw).Replace(
        'assets/lighting/emissive-mesh-stage.gltf', $scenePath.Replace('\', '/'))
    $scriptPath = Join-Path $output "$name.py"
    Set-Content -Encoding utf8 $scriptPath $script
    $bitmap.Dispose(); $writer.Dispose(); $stream.Dispose()
    & (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output $name)
    if ($LASTEXITCODE -ne 0) { throw "Renderer failed for $name" }
}

Make-Scene 'red' ([System.Drawing.Color]::Red)
Make-Scene 'blue' ([System.Drawing.Color]::Blue)

function Floor-Color([string]$name) {
    $bitmap = [System.Drawing.Bitmap]::new((Join-Path $output "$name.png"))
    [double[]]$sum = @(0, 0, 0)
    for ($y = 470; $y -lt 610; $y += 4) {
        for ($x = 420; $x -lt 860; $x += 4) {
            $pixel = $bitmap.GetPixel($x, $y)
            $sum[0] += $pixel.R; $sum[1] += $pixel.G; $sum[2] += $pixel.B
        }
    }
    $bitmap.Dispose()
    return ,$sum
}
$red = Floor-Color 'red'
$blue = Floor-Color 'blue'
Write-Host "Floor RGB totals: red fixture $red; blue fixture $blue"
if ($red[0] -le $blue[0] * 1.3 -or $blue[2] -le $red[2] * 1.3) {
    throw 'Emissive texture did not change the floor lighting color'
}
