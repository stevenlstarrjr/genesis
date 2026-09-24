param(
    [string]$Executable = 'build/release/microsoft/genesis.exe',
    [single]$BoxX = 4.0,
    [switch]$FourPanels
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/emissive-distant-shadow-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$suffix = if ($BoxX -lt 0) { 'left' } elseif ($BoxX -gt 0) { 'right' } else { 'center' }
if ($FourPanels) { $suffix = "four-$suffix" }
$centers = if ($FourPanels) { @(-4.5, -1.5, 1.5, 4.5) } else { @(-4.0, 0.0, 4.0) }
$scene = Get-Content (Join-Path $root 'assets/lighting/emissive-occlusion-stage.gltf') -Raw | ConvertFrom-Json -AsHashtable
$source = [Convert]::FromBase64String(($scene.buffers[0].uri -split ',')[1])
$stream = [System.IO.MemoryStream]::new()
$stream.Write($source, 0, 108) # Floor geometry stays unchanged.
$writer = [System.IO.BinaryWriter]::new($stream)
foreach ($center in $centers) {
    foreach ($vertex in @(@(-0.75,-0.5), @(0.75,-0.5), @(-0.75,0.5), @(0.75,0.5))) {
        $writer.Write([single]($center + $vertex[0]))
        $writer.Write([single]3.0)
        $writer.Write([single]$vertex[1])
    }
}
foreach ($i in 0..($centers.Count * 4 - 1)) {
    $writer.Write([single]0.0); $writer.Write([single]-1.0); $writer.Write([single]0.0)
}
foreach ($base in (0..($centers.Count - 1) | ForEach-Object { $_ * 4 })) {
    foreach ($index in @(0,1,2,1,3,2)) { $writer.Write([uint16]($base + $index)) }
}
# Move the occluder under either distant outer panel.
$box = [byte[]]::new(540)
[Array]::Copy($source, 216, $box, 0, 540)
$boxStream = [System.IO.MemoryStream]::new($box, $true)
$boxWriter = [System.IO.BinaryWriter]::new($boxStream)
$boxReader = [System.IO.BinaryReader]::new($boxStream, [System.Text.Encoding]::UTF8, $true)
for ($vertex = 0; $vertex -lt 20; ++$vertex) {
    $boxStream.Position = $vertex * 12
    $x = $boxReader.ReadSingle()
    $boxStream.Position = $vertex * 12
    $boxWriter.Write([single]($x + $BoxX))
}
$stream.Write($box, 0, $box.Length)
$scene.buffers[0].byteLength = [int]$stream.Length
$scene.buffers[0].uri = 'data:application/octet-stream;base64,' + [Convert]::ToBase64String($stream.ToArray())
$panelVertexBytes = $centers.Count * 4 * 12
$panelIndexBytes = $centers.Count * 6 * 2
$scene.bufferViews[3].byteOffset = 108; $scene.bufferViews[3].byteLength = $panelVertexBytes
$scene.bufferViews[4].byteOffset = 108 + $panelVertexBytes; $scene.bufferViews[4].byteLength = $panelVertexBytes
$scene.bufferViews[5].byteOffset = 108 + 2 * $panelVertexBytes; $scene.bufferViews[5].byteLength = $panelIndexBytes
$scene.bufferViews[6].byteOffset = 108 + 2 * $panelVertexBytes + $panelIndexBytes
$scene.bufferViews[7].byteOffset = $scene.bufferViews[6].byteOffset + 240
$scene.bufferViews[8].byteOffset = $scene.bufferViews[7].byteOffset + 240
$scene.accessors[3].count = $centers.Count * 4
$scene.accessors[3].min = @([single]($centers[0] - 0.75), 3.0, -0.5)
$scene.accessors[3].max = @([single]($centers[-1] + 0.75), 3.0, 0.5)
$scene.accessors[4].count = $centers.Count * 4
$scene.accessors[5].count = $centers.Count * 6
$scene.accessors[6].min = @([single]($BoxX - 0.7), 0.0, -0.7)
$scene.accessors[6].max = @([single]($BoxX + 0.7), 1.6, 0.7)
$scenePath = Join-Path $output "distant-$suffix.gltf"
$scene | ConvertTo-Json -Depth 30 | Set-Content -Encoding utf8 $scenePath
$script = (Get-Content (Join-Path $root 'demos/emissive_mesh_shadows.py') -Raw).Replace(
    'assets/lighting/emissive-occlusion-stage.gltf', $scenePath.Replace('\', '/'))
$scriptPath = Join-Path $output "distant-$suffix.py"
Set-Content -Encoding utf8 $scriptPath $script
$writer.Dispose(); $boxWriter.Dispose(); $boxReader.Dispose(); $boxStream.Dispose(); $stream.Dispose()
$capture = Join-Path $output "distant-$suffix.png"
& (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output "distant-$suffix")
if ($LASTEXITCODE -ne 0) { throw 'Distant-panel shadow render failed' }
$bitmap = [System.Drawing.Bitmap]::new($capture)
$shadowX = if ($BoxX -lt 0) { 880 } elseif ($BoxX -gt 0) { 400 } else { 640 }
$litX = if ($BoxX -lt 0) { 680 } elseif ($BoxX -gt 0) { 600 } else { 820 }
$sampleY = if ($BoxX -eq 0) { 480 } else { 440 }
if ($FourPanels -and $BoxX -gt 0) {
    $shadowX = 200; $litX = 600; $sampleY = 490
} elseif ($FourPanels -and $BoxX -lt 0) {
    $shadowX = 1100; $litX = 680; $sampleY = 490
}
$shadow = $bitmap.GetPixel($shadowX,$sampleY).R
$lit = $bitmap.GetPixel($litX,$sampleY).R
$bitmap.Dispose()
Write-Host "Distant $suffix emitter floor red: shadow $shadow, lit $lit"
if ($shadow -ge 80 -or $lit -le 110) {
    throw 'Distant emitter lost its box shadow or lit floor control'
}
