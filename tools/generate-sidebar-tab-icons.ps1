Add-Type -AssemblyName System.Drawing
$iconDirectory = Join-Path $PSScriptRoot '..\assets\editor_icons\blender'
foreach ($word in @('Item', 'Tool', 'View')) {
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $family = [System.Drawing.FontFamily]::new('Arial')
    $path.AddString($word, $family, 0, 27, [System.Drawing.PointF]::new(0, 0), [System.Drawing.StringFormat]::GenericDefault)
    $bounds = $path.GetBounds()
    $commands = [System.Collections.Generic.List[string]]::new()
    $points = $path.PathPoints
    $types = $path.PathTypes
    $scale = [math]::Min(1.7, 76 / $bounds.Width)
    $centerX = $bounds.X + $bounds.Width / 2
    $centerY = $bounds.Y + $bounds.Height / 2
    $mapped = foreach ($point in $points) {
        [System.Drawing.PointF]::new([float]((50 - ($point.Y - $centerY) * $scale) * 16), [float]((50 + ($point.X - $centerX) * $scale) * 16))
    }
    for ($i = 0; $i -lt $points.Length; $i++) {
        $kind = $types[$i] -band 7
        if ($kind -eq 0) { $commands.Add(('M {0:R} {1:R}' -f $mapped[$i].X, $mapped[$i].Y)) }
        elseif ($kind -eq 1) { $commands.Add(('L {0:R} {1:R}' -f $mapped[$i].X, $mapped[$i].Y)) }
        elseif ($kind -eq 3 -and $i + 2 -lt $points.Length) {
            $commands.Add(('C {0:R} {1:R} {2:R} {3:R} {4:R} {5:R}' -f $mapped[$i].X, $mapped[$i].Y, $mapped[$i+1].X, $mapped[$i+1].Y, $mapped[$i+2].X, $mapped[$i+2].Y))
            $i += 2
        }
        if (($types[$i] -band 128) -ne 0) { $commands.Add('Z') }
    }
    $svg = @"
<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1600" viewBox="0 0 1600 1600">
  <path fill="#e2e2e2" d="$($commands -join ' ')"/>
</svg>
"@
    [System.IO.File]::WriteAllText((Join-Path $iconDirectory "SidebarTab$word.svg"), $svg)
    $path.Dispose(); $family.Dispose()
}
