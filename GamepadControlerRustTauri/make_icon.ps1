Add-Type -AssemblyName System.Drawing

$dir = "l:\GamepadControlerQt\GamepadControlerRustTauri\icons"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$size = 256
$bmp = [System.Drawing.Bitmap]::new($size, $size)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)

$bgBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 35, 37, 41))
$g.FillEllipse($bgBrush, [System.Drawing.Rectangle]::new(8, 8, $size - 16, $size - 16))
Write-Output "bg drawn"

$accentBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 127, 201, 196))
$g.FillEllipse($accentBrush, [System.Drawing.Rectangle]::new(56, 56, $size - 112, $size - 112))
Write-Output "accent drawn"

$pngPath = Join-Path $dir "icon.png"
$bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)

$pngBytes = [System.IO.File]::ReadAllBytes($pngPath)
$ms = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($ms)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]1)
$bw.Write([Byte]0)
$bw.Write([Byte]0)
$bw.Write([Byte]0)
$bw.Write([Byte]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]32)
$bw.Write([UInt32]$pngBytes.Length)
$bw.Write([UInt32]22)
$ms.Write($pngBytes, 0, $pngBytes.Length)
$bw.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $dir "icon.ico"), $ms.ToArray())
$bw.Close()
$g.Dispose()
$bmp.Dispose()
Write-Output ("icon created, ico size: " + (Get-Item (Join-Path $dir "icon.ico")).Length)
