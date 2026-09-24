param([string]$Executable = 'build/release/microsoft/genesis.exe')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/editor-project'
$fixture = Join-Path $output 'project'
foreach ($folder in @('Models','Models/Nested','Images','Scripts','Empty','.genesis')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $fixture $folder) | Out-Null
}
$source = Join-Path $root 'examples/editor_project'
$scene = Get-Content -LiteralPath (Join-Path $source 'main.gscene') -Raw | ConvertFrom-Json
$scene.script = (Resolve-Path (Join-Path $source $scene.script)).Path
foreach ($entity in $scene.entities) {
    if ($entity.model) {
        $model = (Resolve-Path (Join-Path $source $entity.model)).Path
        Copy-Item -LiteralPath $model -Destination (Join-Path $fixture ('Models/' + [IO.Path]::GetFileName($model))) -Force
        $entity.model = 'Models/' + [IO.Path]::GetFileName($model)
    }
}
'# Project browser fixture' | Set-Content -LiteralPath (Join-Path $fixture 'Scripts/game.py')
$sceneFile = Join-Path $fixture 'main.gscene'
$scene | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $sceneFile -Encoding utf8
Add-Type -AssemblyName System.Drawing
$texture=[System.Drawing.Bitmap]::new(64,64)
try {
    for($y=0;$y -lt 64;++$y) { for($x=0;$x -lt 64;++$x) {
        $color=if (([int][Math]::Floor($x/8)+[int][Math]::Floor($y/8))%2) {[System.Drawing.Color]::FromArgb(255,210,95,40)} else {[System.Drawing.Color]::FromArgb(255,40,130,205)}
        $texture.SetPixel($x,$y,$color)
    } }
    $texture.Save((Join-Path $fixture 'Images/checker.png'),[System.Drawing.Imaging.ImageFormat]::Png)
} finally {$texture.Dispose()}
function New-ProjectGroup([string[]]$tabs) {return @{tabs=$tabs;active=$tabs[0]}}
function New-ProjectSplit([string]$axis,[double]$ratio,$first,$second) {return @{axis=$axis;ratio=$ratio;first=$first;second=$second}}
foreach($case in @('browser','project-only')) {
    $layout = if($case -eq 'project-only') {New-ProjectGroup @('project','scene','game','hierarchy','inspector','console')} else {
        New-ProjectSplit 'horizontal' (1116.0/1436) (New-ProjectSplit 'vertical' .60 (New-ProjectSplit 'horizontal' (220.0/1112) (New-ProjectGroup @('hierarchy')) (New-ProjectGroup @('scene','game'))) (New-ProjectGroup @('project','console'))) (New-ProjectGroup @('inspector'))
    }
    @{version=1;workspace=$layout} | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $fixture '.genesis/editor-layout.json') -Encoding utf8
    $before=(Get-FileHash -LiteralPath $sceneFile).Hash
    & (Join-Path $root $Executable) --editor $sceneFile --screenshot (Join-Path $output $case) | Out-Host
    if($LASTEXITCODE -ne 0) {throw "Project browser native capture failed: $case"}
    if((Get-FileHash -LiteralPath $sceneFile).Hash -ne $before) {throw 'Browsing wrote authored scene data'}
    $bitmap=[System.Drawing.Bitmap]::new((Join-Path $output "$case.png"))
    try {
        $thumbnailTop=if($case -eq 'project-only'){123}else{615}
        $colorPixels=0
        for($y=$thumbnailTop;$y -lt $thumbnailTop+60;++$y){for($x=188;$x -lt 900;++$x){
            $pixel=$bitmap.GetPixel($x,$y)
            if(($pixel.R -gt 180 -and $pixel.G -gt 60 -and $pixel.G -lt 130 -and $pixel.B -lt 80) -or ($pixel.B -gt 160 -and $pixel.R -lt 80 -and $pixel.G -gt 90)){++$colorPixels}
        }}
        if($colorPixels -lt 100){throw "Real image thumbnail missing from $case capture: $colorPixels colored pixels"}
        Write-Host "PASS: $case native Project thumbnails ($colorPixels colored pixels), authored file unchanged."
    } finally {$bitmap.Dispose()}
}
