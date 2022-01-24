$Inkscape = 'C:\Program Files\Inkscape\inkscape'
$ImageMagick = 'C:\Program Files\ImageMagick-7.0.10-Q16-HDRI\magick'
#$Sizes = (24, 32, 48, 64)
$Sizes = (48)

Get-ChildItem -Filter '*.svg' | ForEach { 
    $_.Name
   
    ForEach ($size in $Sizes){
        $fileNamePng = "$($_.BaseName)-${size}.png"
        $fileNameHeader = "$($_.BaseName)-${size}.h"
        $fileNamePngTemp = "_$fileNamePng"
        & $Inkscape -h $size -w $size --export-area-snap --export-area-drawing --export-background=white --export-png="`"$fileNamePngTemp`"" $_.Name | Out-Null
        & $ImageMagick $fileNamePngTemp -threshold 80% $fileNamePng
        Remove-Item $fileNamePngTemp
        & $ImageMagick $fileNamePng $fileNameHeader
    }
    Remove-Item $_.Name
}