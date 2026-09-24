param([string]$Executable = 'build/release/microsoft/genesis.exe')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/editor-docking'
$source = Join-Path $root 'examples/editor_project'
$scene = Get-Content -LiteralPath (Join-Path $source 'main.gscene') -Raw | ConvertFrom-Json
$scene.script = (Resolve-Path (Join-Path $source $scene.script)).Path
foreach ($entity in $scene.entities) {
    if ($entity.model) { $entity.model = (Resolve-Path (Join-Path $source $entity.model)).Path }
}
function New-DockGroup([string[]]$tabs) { return @{ tabs=$tabs; active=$tabs[0] } }
function New-DockSplit([string]$axis,[double]$ratio,$first,$second) { return @{axis=$axis;ratio=$ratio;first=$first;second=$second} }
$wide = New-DockSplit 'vertical' (590.0/822) (New-DockSplit 'horizontal' (1116.0/1436) (New-DockGroup @('scene')) (New-DockSplit 'vertical' .4 (New-DockGroup @('hierarchy')) (New-DockGroup @('inspector')))) (New-DockGroup @('project','console'))
$hidden = New-DockGroup @('console','scene','project','hierarchy','inspector')
foreach ($case in @('default','wide','hidden-scene')) {
    $folder=Join-Path $output $case
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $sceneFile=Join-Path $folder 'main.gscene'
    $scene | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $sceneFile -Encoding utf8
    $layout = if ($case -eq 'wide') { $wide } elseif ($case -eq 'hidden-scene') { $hidden } else {
        New-DockSplit 'horizontal' (1116.0/1436) (New-DockSplit 'vertical' (590.0/822) (New-DockSplit 'horizontal' (220.0/1112) (New-DockGroup @('hierarchy')) (New-DockGroup @('scene'))) (New-DockGroup @('project','console'))) (New-DockGroup @('inspector'))
    }
    $settings=Join-Path $folder '.genesis'
    New-Item -ItemType Directory -Force -Path $settings | Out-Null
    @{version=1;workspace=$layout} | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $settings 'editor-layout.json') -Encoding utf8
    & (Join-Path $root $Executable) --editor $sceneFile --screenshot (Join-Path $output $case) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Native docking capture failed: $case" }
}
Add-Type -AssemblyName System.Drawing
foreach ($case in @('default','wide','hidden-scene')) {
    $bitmap=[System.Drawing.Bitmap]::new((Join-Path $output "$case.png"))
    try {
        if ($bitmap.Width -ne 1440 -or $bitmap.Height -ne 900) { throw 'Unexpected capture dimensions' }
        $colors=[System.Collections.Generic.HashSet[int]]::new()
        for($y=200;$y -lt 580;$y+=8) { for($x=300;$x -lt 1000;$x+=8) { $colors.Add($bitmap.GetPixel($x,$y).ToArgb()) | Out-Null } }
        if ($case -eq 'hidden-scene') {
            if ($colors.Count -ne 1 -or $bitmap.GetPixel(600,400).R -ne 56) { throw 'Inactive Scene leaked through the Console panel' }
        } elseif ($colors.Count -lt 100) { throw "Live scene did not render inside $case workspace" }
        Write-Host "PASS: $case workspace native capture ($($colors.Count) sampled colors)."
    } finally { $bitmap.Dispose() }
}
