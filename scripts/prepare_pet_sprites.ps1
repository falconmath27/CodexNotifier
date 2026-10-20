param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot "..\PET_DESIGN"),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\app\assets\pets")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Test-BackgroundPixel {
    param([System.Drawing.Color]$Color)

    $minimum = [Math]::Min($Color.R, [Math]::Min($Color.G, $Color.B))
    $maximum = [Math]::Max($Color.R, [Math]::Max($Color.G, $Color.B))
    return $Color.A -eq 0 -or ($minimum -ge 218 -and ($maximum - $minimum) -le 38)
}

function Remove-ConnectedBackground {
    param([System.Drawing.Bitmap]$Bitmap)

    $visited = New-Object 'bool[,]' $Bitmap.Width, $Bitmap.Height
    $queue = [System.Collections.Generic.Queue[System.Drawing.Point]]::new()

    function Add-BackgroundPixel([int]$X, [int]$Y) {
        if ($X -lt 0 -or $X -ge $Bitmap.Width -or $Y -lt 0 -or $Y -ge $Bitmap.Height) { return }
        if ($visited[$X, $Y]) { return }
        $visited[$X, $Y] = $true
        if (Test-BackgroundPixel $Bitmap.GetPixel($X, $Y)) {
            $queue.Enqueue([System.Drawing.Point]::new($X, $Y))
        }
    }

    for ($x = 0; $x -lt $Bitmap.Width; ++$x) {
        Add-BackgroundPixel $x 0
        Add-BackgroundPixel $x ($Bitmap.Height - 1)
    }
    for ($y = 0; $y -lt $Bitmap.Height; ++$y) {
        Add-BackgroundPixel 0 $y
        Add-BackgroundPixel ($Bitmap.Width - 1) $y
    }

    while ($queue.Count -gt 0) {
        $point = $queue.Dequeue()
        $Bitmap.SetPixel($point.X, $point.Y, [System.Drawing.Color]::Transparent)
        Add-BackgroundPixel ($point.X - 1) $point.Y
        Add-BackgroundPixel ($point.X + 1) $point.Y
        Add-BackgroundPixel $point.X ($point.Y - 1)
        Add-BackgroundPixel $point.X ($point.Y + 1)
    }
}

function Export-SpriteSheet {
    param(
        [string]$InputFile,
        [string]$StateName,
        [int]$FrameCount = 4
    )

    $sourcePath = Join-Path $SourceDirectory $InputFile
    if (-not (Test-Path -LiteralPath $sourcePath)) {
        throw "Missing sprite sheet: $sourcePath"
    }

    $stateDirectory = Join-Path $OutputDirectory $StateName
    New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null
    $source = [System.Drawing.Bitmap]::FromFile($sourcePath)
    try {
        if (($source.Width % $FrameCount) -ne 0) {
            throw "$InputFile width must divide evenly into $FrameCount frames."
        }

        $frameWidth = [int]($source.Width / $FrameCount)
        for ($index = 0; $index -lt $FrameCount; ++$index) {
            $frame = [System.Drawing.Bitmap]::new(
                $frameWidth,
                $source.Height,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
            )
            try {
                $graphics = [System.Drawing.Graphics]::FromImage($frame)
                try {
                    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                    $graphics.DrawImage(
                        $source,
                        [System.Drawing.Rectangle]::new(0, 0, $frameWidth, $source.Height),
                        [System.Drawing.Rectangle]::new($index * $frameWidth, 0, $frameWidth, $source.Height),
                        [System.Drawing.GraphicsUnit]::Pixel
                    )
                }
                finally {
                    $graphics.Dispose()
                }

                Remove-ConnectedBackground $frame
                $outputPath = Join-Path $stateDirectory ("frame_{0}.png" -f ($index + 1))
                $frame.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
                Write-Output $outputPath
            }
            finally {
                $frame.Dispose()
            }
        }
    }
    finally {
        $source.Dispose()
    }
}

function Export-PaddedFrames {
    param(
        [string]$InputDirectory,
        [string]$StateName
    )

    $sourceDirectoryPath = Join-Path $SourceDirectory $InputDirectory
    $sourceFiles = @(Get-ChildItem -LiteralPath $sourceDirectoryPath -Filter "*.png" | Sort-Object Name)
    if ($sourceFiles.Count -eq 0) {
        throw "No PNG frames found in: $sourceDirectoryPath"
    }

    $dimensions = foreach ($sourceFile in $sourceFiles) {
        $image = [System.Drawing.Bitmap]::FromFile($sourceFile.FullName)
        try { [pscustomobject]@{ Width = $image.Width; Height = $image.Height } }
        finally { $image.Dispose() }
    }
    $canvasWidth = ($dimensions | Measure-Object -Property Width -Maximum).Maximum
    $canvasHeight = ($dimensions | Measure-Object -Property Height -Maximum).Maximum
    $stateDirectory = Join-Path $OutputDirectory $StateName
    New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null

    for ($index = 0; $index -lt $sourceFiles.Count; ++$index) {
        $source = [System.Drawing.Bitmap]::FromFile($sourceFiles[$index].FullName)
        try {
            $frame = [System.Drawing.Bitmap]::new(
                $canvasWidth,
                $canvasHeight,
                [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
            )
            try {
                $graphics = [System.Drawing.Graphics]::FromImage($frame)
                try {
                    $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                    $graphics.Clear([System.Drawing.Color]::Transparent)
                    $x = [int](($canvasWidth - $source.Width) / 2)
                    $y = $canvasHeight - $source.Height
                    $graphics.DrawImageUnscaled($source, $x, $y)
                }
                finally {
                    $graphics.Dispose()
                }

                $outputPath = Join-Path $stateDirectory ("frame_{0}.png" -f ($index + 1))
                $frame.Save($outputPath, [System.Drawing.Imaging.ImageFormat]::Png)
                Write-Output $outputPath
            }
            finally {
                $frame.Dispose()
            }
        }
        finally {
            $source.Dispose()
        }
    }
}

Export-SpriteSheet -InputFile "FRAME1.jpg" -StateName "working"
Export-SpriteSheet -InputFile "FRAME2.jpg" -StateName "approval"
Export-PaddedFrames -InputDirectory "smiling-orange-blob" -StateName "idle"
