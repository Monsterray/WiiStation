# dvd_image.ps1 -Out FILE.iso -GameRoot DIR [-Games "Name1","Name2"] [-Folder "PSX Games"]
#
# A test disc for Load from DVD (Gamecube/fileBrowser/fileBrowser-DVD.c): an ISO9660 + Joliet
# image with the named game folders of GameRoot under one folder, made by Windows' own IMAPI2
# -- the engine behind Explorer's "Burn to disc", so it has the same layout a user's burned
# disc has. IMAPI2 records each directory's size in bytes (120, not 2048), which the old
# libiso9660 looped on; keep it as the test.
#
# Dolphin inserts only a disc it takes for a GameCube or Wii one, so the image gets a
# GameCube header (game ID GWSX01, magic C2339F3D at 0x1C) in ISO9660's system area
# (bytes 0-0x7FFF, which ISO9660 leaves unused). A real Wii ignores it; burn the image as it
# is, or burn the folders directly.
#
# Then boot with the disc in Dolphin's drive:
#   DOLPHIN_ARGS="-C Dolphin.Core.DefaultISO=C:/path/test.iso" bash scripts/wsx.sh chain ...
# with chain lines "dvd:/PSX Games/<game>" and ".cue", or autoinput "menupage 72".
param(
    [Parameter(Mandatory)] [string]$Out,
    [Parameter(Mandatory)] [string]$GameRoot,
    [string[]]$Games = @('Ape Escape', 'Frogger (USA)'),
    [string]$Folder = 'PSX Games'
)
$ErrorActionPreference = 'Stop'

$fs = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
$fs.FileSystemsToCreate = 3          # ISO9660 | Joliet
$fs.FreeMediaBlocks = 2295104        # a single-layer DVD
$fs.VolumeName = 'WIISTATION_TEST'
$fs.Root.AddDirectory($Folder)
$top = $fs.Root.Item($Folder)
foreach ($g in $Games) {
    $top.AddDirectory($g)
    $d = $top.Item($g)
    Get-ChildItem -LiteralPath (Join-Path $GameRoot $g) -File | ForEach-Object {
        $stream = New-Object -ComObject ADODB.Stream
        $stream.Type = 1
        $stream.Open()
        $stream.LoadFromFile($_.FullName)
        $d.AddFile($_.Name, $stream)
    }
}
$result = $fs.CreateResultImage()

# IStream -> file: COM's IStream has no PowerShell copy
Add-Type -TypeDefinition @"
using System; using System.IO; using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
public static class WsxIsoWriter {
    public static void Save(object o, string path) {
        IStream s = (IStream)o;
        using (FileStream f = File.Create(path)) {
            byte[] buf = new byte[2048 * 64];
            IntPtr pRead = Marshal.AllocHGlobal(4);
            for (;;) {
                s.Read(buf, buf.Length, pRead);
                int n = Marshal.ReadInt32(pRead);
                if (n <= 0) break;
                f.Write(buf, 0, n);
            }
            Marshal.FreeHGlobal(pRead);
        }
    }
}
"@
[WsxIsoWriter]::Save($result.ImageStream, $Out)

# The GameCube header, for Dolphin
$hdr = New-Object byte[] 0x440
[Text.Encoding]::ASCII.GetBytes('GWSX01').CopyTo($hdr, 0)
[byte[]](0xC2, 0x33, 0x9F, 0x3D) | ForEach-Object -Begin { $i = 0x1C } -Process { $hdr[$i++] = $_ }
[Text.Encoding]::ASCII.GetBytes('WiiStation DVD test (ISO9660 inside)').CopyTo($hdr, 0x20)
$f = [IO.File]::Open($Out, 'Open', 'ReadWrite')
$f.Write($hdr, 0, $hdr.Length)
$f.Close()
"wrote ${Out}: $($result.TotalBlocks) blocks of $($result.BlockSize)"
