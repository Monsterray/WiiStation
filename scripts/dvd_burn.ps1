# dvd_burn.ps1 -GameRoot DIR -Games "A","B",... [-Folder "PSX Games"] [-Finalize] [-DryRun]
#
# Burns PS1 game folders to a blank DVD for WiiStation's Load from DVD, with Windows' own
# burning engine (IMAPI2, as Explorer does): one session, ISO9660 + Joliet, the games in
# <Folder>/<game>/ -- the layout tests/dvd chains use (dvd:/PSX Games/<game>). No GameCube
# header (that is only for Dolphin: scripts/dvd_image.ps1).
#
# -Finalize closes the disc (nothing can be added later; the most compatible). Without it the
#  session is closed but the disc stays appendable -- WiiStation reads the first session
#  only, so games added in a later burn do not appear on the Wii.
# -DryRun checks the media and the sizes and writes nothing.
#
# A Wii reads DVD-R and, on some drives, DVD+R; no Wii reads CDs. Many Wiis made after 2008
# cannot read burned DVDs at all (Gamecube/fileBrowser/fileBrowser-DVD.c).
param(
    [Parameter(Mandatory)] [string]$GameRoot,
    [Parameter(Mandatory)] [string[]]$Games,
    [string]$Folder = 'PSX Games',
    [string]$VolumeName = 'WIISTATION_PS1',
    [switch]$Finalize,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

$dm = New-Object -ComObject IMAPI2.MsftDiscMaster2
if ($dm.Count -lt 1) { throw 'no disc burner' }
$rec = New-Object -ComObject IMAPI2.MsftDiscRecorder2
$rec.InitializeDiscRecorder($dm.Item(0))
$fmt = New-Object -ComObject IMAPI2.MsftDiscFormat2Data
$fmt.Recorder = $rec
$fmt.ClientName = 'WiiStation dvd_burn.ps1'
if (-not $fmt.IsCurrentMediaSupported($rec)) { throw 'the disc in the burner cannot take data' }
$names = @{6='DVD+R';7='DVD+RW';9='DVD-R';10='DVD-RW';8='DVD+R DL';11='DVD-R DL'}
$media = $names[[int]$fmt.CurrentPhysicalMediaType]
if (-not $media) { throw "not a DVD (media type $($fmt.CurrentPhysicalMediaType)): a Wii cannot read CDs" }
if (-not ($fmt.CurrentMediaStatus -band 2)) { throw "the $media is not blank" }
$free = [int64]$fmt.FreeSectorsOnMedia * 2048

$fs = New-Object -ComObject IMAPI2FS.MsftFileSystemImage
$fs.ChooseImageDefaults($rec)
$fs.FileSystemsToCreate = 3          # ISO9660 | Joliet: what WiiStation's libiso9660 reads
$fs.VolumeName = $VolumeName
$fs.Root.AddDirectory($Folder)
$top = $fs.Root.Item($Folder)
$bytes = 0
foreach ($g in $Games) {
    $src = Join-Path $GameRoot $g
    if (-not (Test-Path -LiteralPath $src)) { throw "no folder: $src" }
    $top.AddDirectory($g)
    $d = $top.Item($g)
    Get-ChildItem -LiteralPath $src -File | ForEach-Object {
        $bytes += $_.Length
        $s = New-Object -ComObject ADODB.Stream
        $s.Type = 1
        $s.Open()
        $s.LoadFromFile($_.FullName)
        $d.AddFile($_.Name, $s)
    }
}
$image = $fs.CreateResultImage()
$need = [int64]$image.TotalBlocks * 2048
"{0}: {1} games, {2:N0} bytes of files, image {3:N0} bytes, free {4:N0} ({5:N0} spare); finalize: {6}" -f `
    $media, $Games.Count, $bytes, $need, $free, ($free - $need), [bool]$Finalize
if ($need -gt $free) { throw 'the games do not fit' }
if ($DryRun) { 'dry run: nothing written'; return }

$fmt.ForceMediaToBeClosed = [bool]$Finalize
$t0 = Get-Date
"burning to $($rec.VolumePathNames -join ',') ..."
$fmt.Write($image.ImageStream)
"done in {0:N0} s" -f ((Get-Date) - $t0).TotalSeconds
