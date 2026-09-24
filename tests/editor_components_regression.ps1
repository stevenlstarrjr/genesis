param([string]$Executable = 'build/release/microsoft/genesis.exe')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/editor-components'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$setup = Join-Path $output 'environment.py'
@'
import genesis as gx
gx.window.configure(title="Component Inspector", width=1440, height=900, vsync=False)
gx.renderer.quality("medium", gpu_timings=True)
gx.color.agx(look="neutral", exposure=0, auto_exposure=False)
gx.sky.earth().sun(azimuth=145, elevation=45, intensity=0.1).radiance(0.03)
gx.camera.look_at(position=(3, 2, 4), target=(0, 0, 0), fov=50)
'@ | Set-Content -LiteralPath $setup -Encoding utf8
$model = Join-Path $root 'assets/glTF-Sample-Models/2.0/Box/glTF-Binary/Box.glb'
function Capture-Components([string]$name, [double[]]$color, [bool]$lit, [string]$selection) {
    $mesh = @{name='Material Preview';model=$model;position=@(0,0,0);material=@{color=$color;metallic=0;roughness=0.4}}
    $light = @{name='Point Light';position=@(1.5,2,2);light=@{type='point';enabled=$lit;color=@(1,1,1);intensity=25;range=10;shadows=$true}}
    $probe = @{name='Reflection Probe';position=@(0,0,0);reflection_probe=@{size=@(6,6,6);blend=1;priority=2}}
    $entities = if ($selection -eq 'light') { @($mesh,$probe,$light) } elseif ($selection -eq 'probe') { @($mesh,$light,$probe) } else { @($light,$probe,$mesh) }
    $scene = @{name='Component Inspector';script=$setup;entities=$entities;editor_camera=@{position=@(3,2,4);target=@(0,0,0);orbit_pivot=@(0,0,0);fov=50}}
    $sceneFile=Join-Path $output "$name.gscene"
    $scene | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $sceneFile -Encoding utf8
    $capture=Join-Path $output $name
    & (Join-Path $root $Executable) --editor $sceneFile --screenshot $capture | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Native component capture failed: $name" }
    return "$capture.png"
}
Add-Type -AssemblyName System.Drawing
function Average-Object([string]$path) {
    $bitmap=[System.Drawing.Bitmap]::new($path)
    try {
        [double[]]$sum=@(0,0,0)
        for($y=343;$y -lt 403;$y++) { for($x=640;$x -lt 700;$x++) {
            $pixel=$bitmap.GetPixel($x,$y);$sum[0]+=$pixel.R;$sum[1]+=$pixel.G;$sum[2]+=$pixel.B
        }}
        return @(($sum[0]/3600),($sum[1]/3600),($sum[2]/3600))
    } finally { $bitmap.Dispose() }
}
$red=Capture-Components 'material-red' @(1,.02,.02) $true 'mesh'
$blue=Capture-Components 'material-blue' @(.02,.02,1) $true 'mesh'
$dark=Capture-Components 'light-disabled' @(1,.02,.02) $false 'light'
$probe=Capture-Components 'probe-inspector' @(.7,.7,.7) $true 'probe'
$r=Average-Object $red
$b=Average-Object $blue
$d=Average-Object $dark
if($r[0] -le $r[2]+15 -or $b[2] -le $b[0]+15) { throw "Material override did not change rendered color: red=$r blue=$b" }
if($r[0] -le $d[0]+10) { throw "Authored point light did not illuminate the object: lit=$r dark=$d" }
Write-Host "PASS: native material and light readbacks. Red=$r Blue=$b Disabled=$d"
