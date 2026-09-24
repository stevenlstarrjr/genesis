param(
    [string]$Executable = 'build/release/microsoft/genesis.exe',
    [ValidateRange(2, 4)][int]$CompetingPoints = 2,
    [switch]$OutOfRangePoints,
    [switch]$BackFacingArea,
    [switch]$BackFacingSpot,
    [switch]$FacingSpot,
    [switch]$SidewaysSpot,
    [switch]$ClusteredPoints,
    [switch]$ExpectAreaUnshadowed
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $root 'artifacts/local-shadow-third-slot-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$script = Get-Content (Join-Path $root 'demos/area_light_shadows.py') -Raw
$pointLights = for ($index = 0; $index -lt $CompetingPoints; ++$index) {
    if ($OutOfRangePoints) {
        $position = "(1, 8, $(11 + 2 * $index))"
        $radius = 1
        $intensity = 1000
    } elseif ($ClusteredPoints) {
        $z = (5.0 - 0.3 * $index).ToString('0.0', [System.Globalization.CultureInfo]::InvariantCulture)
        $position = "(5, 2, $z)"
        $radius = 2
        $intensity = 350
    } else {
        $position = "(5, 2, $(5 - $index))"
        $radius = 2
        $intensity = 1000
    }
    "gx.lights.point(`"Candidate $index`", position=$position, color=(1, 1, 1), intensity=$intensity, radius=$radius)"
}
$extraLights = ($pointLights -join "`n") + "`n"
if ($BackFacingArea) {
    $extraLights += 'gx.lights.area("Away from scene", position=(1, 8, 11), direction=(0, 1, 0), up=(0, 0, 1), color=(1, 1, 1), intensity=1000, radius=15, width=1, height=1)' + "`n"
}
if ($BackFacingSpot -or $FacingSpot -or $SidewaysSpot) {
    $spotDirection = if ($FacingSpot) { '(0, -1, 0)' } elseif ($SidewaysSpot) { '(1, 0, 0)' } else { '(0, 1, 0)' }
    $extraLights += "gx.lights.spot(`"Directional candidate`", position=(1, 8, 11), direction=$spotDirection, color=(1, 1, 1), intensity=1000, radius=15, inner_angle=20, outer_angle=35)" + "`n"
}
$extraLights += 'gx.lights.area('
$script = $script.Replace('gx.lights.area(', $extraLights)
$slotName = if ($SidewaysSpot) { 'sideways-spot' } elseif ($FacingSpot) { 'facing-spot' } elseif ($BackFacingSpot) { 'back-facing-spot' } elseif ($BackFacingArea) { 'back-facing' } elseif ($OutOfRangePoints) { 'five-candidates' } elseif ($ClusteredPoints) { 'clustered' } elseif ($ExpectAreaUnshadowed) { 'spread' } elseif ($CompetingPoints -eq 2) { 'third' } else { 'fourth' }
$scriptPath = Join-Path $output "area-$slotName-slot.py"
Set-Content -Encoding utf8 $scriptPath $script
$capture = Join-Path $output "area-$slotName-slot.png"
& (Join-Path $root $Executable) --script $scriptPath --screenshot (Join-Path $output "area-$slotName-slot")
if ($LASTEXITCODE -ne 0) { throw "$slotName-slot area shadow render failed" }
$bitmap = [System.Drawing.Bitmap]::new($capture)
$shadow = $bitmap.GetPixel(530,340).R
$lit = $bitmap.GetPixel(800,340).R
$bitmap.Dispose()
Write-Host "$slotName area shadow floor red: shadow $shadow, lit $lit"
if ($ExpectAreaUnshadowed) {
    if ($shadow -le 100 -or $lit -le 130) {
        throw 'Separated high-score point lights did not retain all four shadow captures'
    }
    return
}
if ($shadow -ge 80 -or $lit -le 130) {
    throw "The area light lost its shadow with $CompetingPoints competing point lights"
}
