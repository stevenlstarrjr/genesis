param([string]$Executable = 'build/release/microsoft/genesis.exe')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/editor-play'
$source = Join-Path $root 'examples/editor_project'
$scene = Get-Content -LiteralPath (Join-Path $source 'main.gscene') -Raw | ConvertFrom-Json
$scene.script = (Resolve-Path (Join-Path $source $scene.script)).Path
foreach ($entity in $scene.entities) {
    if ($entity.model) { $entity.model = (Resolve-Path (Join-Path $source $entity.model)).Path }
}
function New-PlayGroup([string[]]$tabs) { return @{tabs=$tabs;active=$tabs[0]} }
foreach ($case in @('game','scene-game','hidden','standalone')) {
    $folder = Join-Path $output $case
    New-Item -ItemType Directory -Force -Path (Join-Path $folder '.genesis') | Out-Null
    $sceneFile = Join-Path $folder 'main.gscene'
    $scene | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $sceneFile -Encoding utf8
    $layout = if ($case -eq 'scene-game') {
        @{axis='horizontal';ratio=.5;first=(New-PlayGroup @('scene'));second=(New-PlayGroup @('game'))}
    } elseif ($case -eq 'hidden') { New-PlayGroup @('console','game','scene','hierarchy','inspector','project') }
    else { New-PlayGroup @('game','scene','hierarchy','inspector','project','console') }
    @{version=1;workspace=$layout} | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $folder '.genesis/editor-layout.json') -Encoding utf8
    $before = (Get-FileHash -LiteralPath $sceneFile).Hash
    $mode = if ($case -eq 'standalone') { '--scene' } else { '--editor' }
    & (Join-Path $root $Executable) $mode $sceneFile --screenshot (Join-Path $output $case) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Native playback capture failed: $case" }
    if ((Get-FileHash -LiteralPath $sceneFile).Hash -ne $before) { throw "Rendering changed the authored scene: $case" }
}
Add-Type -AssemblyName System.Drawing
foreach ($case in @('game','scene-game','hidden','standalone')) {
    $bitmap=[System.Drawing.Bitmap]::new((Join-Path $output "$case.png"))
    try {
        foreach ($side in 0..1) {
            $colors=[System.Collections.Generic.HashSet[int]]::new()
            $x0=if ($side -eq 0) { 40 } else { [int]($bitmap.Width*.5)+40 }
            $x1=if ($side -eq 0) { [int]($bitmap.Width*.5)-40 } else { $bitmap.Width-40 }
            for($y=180;$y -lt $bitmap.Height-100;$y+=6) { for($x=$x0;$x -lt $x1;$x+=6) { $colors.Add($bitmap.GetPixel($x,$y).ToArgb()) | Out-Null } }
            if ($case -eq 'hidden') {
                if ($colors.Count -ne 1 -or $bitmap.GetPixel($x0,400).R -ne 56) { throw 'Hidden Game or Scene leaked into Console' }
            } elseif ($colors.Count -lt 100) { throw "Camera did not render in $case, side $side ($($colors.Count) colors)" }
        }
        Write-Host "PASS: $case native rendering and authored scene preservation."
    } finally { $bitmap.Dispose() }
}
