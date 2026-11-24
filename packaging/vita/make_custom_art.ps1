# Your own LiveArea pictures for the Vita package (docs/dev/VITA.md), from any two images: a
# background, and an icon that is also the start button. Windows PowerShell 5.1 with WPF (built
# in, WebP through Windows' own codec). Written to packaging/vita/custom/sce_sys, which git
# ignores and the build prefers to the placeholders when it is there (reconfigure after making it).
#
#   powershell -File packaging/vita/make_custom_art.ps1 -Background red.webp -Icon logo.png
#
# The Vita wants 8-bit palette PNGs: icon0.png 128x128, livearea bg.png 840x500, startup.png 280x158.
param(
    [Parameter(Mandatory = $true)][string]$Background,
    [Parameter(Mandatory = $true)][string]$Icon,
    [string]$Out = (Join-Path $PSScriptRoot 'custom\sce_sys')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore, WindowsBase

# The PNG written by hand: only IHDR (8-bit palette), PLTE, IDAT and IEND. WPF's encoder adds sRGB,
# gAMA and pHYs and writes a grey-only palette as greyscale; the Vita's installer refuses either
# (error 0x8010113D at the end of the install).
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.IO.Compression;
using System.Text;

public static class VitaPng {
    static readonly uint[] Table = MakeTable();

    static uint[] MakeTable() {
        var t = new uint[256];
        for (uint n = 0; n < 256; n++) {
            uint c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }

    static void U32(Stream s, uint v) {
        s.WriteByte((byte)(v >> 24)); s.WriteByte((byte)(v >> 16)); s.WriteByte((byte)(v >> 8)); s.WriteByte((byte)v);
    }

    static void Chunk(Stream s, string kind, byte[] data) {
        byte[] k = Encoding.ASCII.GetBytes(kind);
        U32(s, (uint)data.Length);
        s.Write(k, 0, 4);
        s.Write(data, 0, data.Length);
        uint c = 0xFFFFFFFFu;
        foreach (byte b in k) c = Table[(c ^ b) & 0xFF] ^ (c >> 8);
        foreach (byte b in data) c = Table[(c ^ b) & 0xFF] ^ (c >> 8);
        U32(s, c ^ 0xFFFFFFFFu);
    }

    // `rgb`: the palette (3 bytes an entry, at most 256); `pixels`: one palette index a pixel, by rows.
    public static void Write(string path, int w, int h, byte[] rgb, byte[] pixels) {
        var raw = new byte[(w + 1) * h]; // each row: filter 0, then its indices
        for (int y = 0; y < h; y++) Buffer.BlockCopy(pixels, y * w, raw, y * (w + 1) + 1, w);
        byte[] z;
        using (var ms = new MemoryStream()) {
            ms.WriteByte(0x78); ms.WriteByte(0x9C); // zlib header (deflate, default)
            using (var ds = new DeflateStream(ms, CompressionMode.Compress, true)) ds.Write(raw, 0, raw.Length);
            uint a = 1, b = 0;
            foreach (byte v in raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
            U32(ms, (b << 16) | a); // Adler-32
            z = ms.ToArray();
        }
        var ihdr = new byte[13];
        ihdr[0] = (byte)(w >> 24); ihdr[1] = (byte)(w >> 16); ihdr[2] = (byte)(w >> 8); ihdr[3] = (byte)w;
        ihdr[4] = (byte)(h >> 24); ihdr[5] = (byte)(h >> 16); ihdr[6] = (byte)(h >> 8); ihdr[7] = (byte)h;
        ihdr[8] = 8; ihdr[9] = 3; // 8-bit, palette; compression, filter, interlace 0
        using (var f = File.Create(path)) {
            f.Write(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }, 0, 8);
            Chunk(f, "IHDR", ihdr);
            Chunk(f, "PLTE", rgb);
            Chunk(f, "IDAT", z);
            Chunk(f, "IEND", new byte[0]);
        }
    }
}
'@

function Load-Image([string]$path) {
    $full = (Resolve-Path $path).ProviderPath
    $decoder = [System.Windows.Media.Imaging.BitmapDecoder]::Create([Uri]$full,
        [System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,
        [System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
    return $decoder.Frames[0]
}

# `src` drawn into a w x h black picture at (x, y, dw, dh), saved as an 8-bit palette PNG.
function Save-Picture($src, [int]$w, [int]$h, [double]$x, [double]$y, [double]$dw, [double]$dh, [string]$path) {
    $visual = New-Object System.Windows.Media.DrawingVisual
    $dc = $visual.RenderOpen()
    $dc.DrawRectangle([System.Windows.Media.Brushes]::Black, $null, (New-Object System.Windows.Rect 0, 0, $w, $h))
    $dc.DrawImage($src, (New-Object System.Windows.Rect $x, $y, $dw, $dh))
    $dc.Close()
    $rgb = New-Object System.Windows.Media.Imaging.RenderTargetBitmap $w, $h, 96, 96, ([System.Windows.Media.PixelFormats]::Pbgra32)
    $rgb.Render($visual)
    $palette = New-Object System.Windows.Media.Imaging.BitmapPalette $rgb, 256
    $indexed = New-Object System.Windows.Media.Imaging.FormatConvertedBitmap $rgb, ([System.Windows.Media.PixelFormats]::Indexed8), $palette, 0
    $pixels = New-Object byte[] ($w * $h)
    $indexed.CopyPixels($pixels, $w, 0)
    $colors = $indexed.Palette.Colors
    $pal = New-Object byte[] (3 * $colors.Count)
    for ($i = 0; $i -lt $colors.Count; $i++) {
        $pal[3 * $i] = $colors[$i].R; $pal[3 * $i + 1] = $colors[$i].G; $pal[3 * $i + 2] = $colors[$i].B
    }
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    [VitaPng]::Write($path, $w, $h, $pal, $pixels)
    Write-Output ("{0}  {1}x{2}, {3} colours" -f $path, $w, $h, $colors.Count)
}

$bg = Load-Image $Background
$logo = Load-Image $Icon

# Background: the picture covers 840x500, kept to its left edge (a corner logo there stays whole).
$scale = [Math]::Max(840 / $bg.PixelWidth, 500 / $bg.PixelHeight)
Save-Picture $bg 840 500 0 ((500 - $bg.PixelHeight * $scale) / 2) ($bg.PixelWidth * $scale) ($bg.PixelHeight * $scale) `
    (Join-Path $Out 'livearea\contents\bg.png')

# Icon: the logo filling 128x128.
Save-Picture $logo 128 128 0 0 128 128 (Join-Path $Out 'icon0.png')

# Start button: the logo as tall as the 280x158 picture, centred on black.
$s = 158 / [Math]::Max($logo.PixelWidth, $logo.PixelHeight)
$lw = $logo.PixelWidth * $s; $lh = $logo.PixelHeight * $s
Save-Picture $logo 280 158 ((280 - $lw) / 2) ((158 - $lh) / 2) $lw $lh (Join-Path $Out 'livearea\contents\startup.png')
