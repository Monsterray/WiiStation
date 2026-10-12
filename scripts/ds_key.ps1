# ds_key.ps1 KEY [HOLD_MS] -- press KEY in the test DuckStation (C:\tools\DuckStation) without
# touching the user's keyboard, mouse or focus: WM_KEYDOWN/WM_KEYUP are posted to DuckStation's
# own windows only (user rule 2026-10-12: never drive the user's input). KEY: Return, F10, K, W ...
param([Parameter(Mandatory)][string]$Key, [int]$HoldMs = 120, [switch]$Children, [int]$Repeat = 1, [int]$IntervalMs = 300)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -Namespace DS -Name W -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
public delegate bool EnumProc(System.IntPtr h, System.IntPtr p);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, System.IntPtr p);
[DllImport("user32.dll")] public static extern bool EnumChildWindows(System.IntPtr parent, EnumProc f, System.IntPtr p);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
'@
$proc = Get-Process | Where-Object { $_.Path -like 'C:\tools\DuckStation\*' } | Select-Object -First 1
if (-not $proc) { Write-Error 'no DuckStation running from C:\tools\DuckStation'; exit 2 }
$vk = [uint32][System.Windows.Forms.Keys]::$Key
$scan = [DS.W]::MapVirtualKey($vk, 0)
$down = [IntPtr](1 -bor ($scan -shl 16))
$up = [IntPtr]([int64]1 -bor ([int64]$scan -shl 16) -bor 0xC0000000)
$targets = New-Object System.Collections.Generic.List[IntPtr]
$cb = [DS.W+EnumProc]{ param($h, $p) $o = 0; [void][DS.W]::GetWindowThreadProcessId($h, [ref]$o); if ($o -eq $proc.Id) { $targets.Add($h) }; $true }
[void][DS.W]::EnumWindows($cb, [IntPtr]::Zero)
if ($Children) { $top = @($targets); $targets.Clear(); foreach ($t in $top) { [void][DS.W]::EnumChildWindows($t, $cb, [IntPtr]::Zero) } }   # -Children: the native child windows (the display) instead
for ($r = 0; $r -lt $Repeat; $r++) {   # -Repeat: a burst (screenshots), IntervalMs apart
	foreach ($t in $targets) { [void][DS.W]::PostMessage($t, 0x100, [IntPtr]$vk, $down) }
	Start-Sleep -Milliseconds $HoldMs
	foreach ($t in $targets) { [void][DS.W]::PostMessage($t, 0x101, [IntPtr]$vk, $up) }
	if ($r -lt $Repeat - 1) { Start-Sleep -Milliseconds $IntervalMs }
}
"posted $Key to $($targets.Count) DuckStation window(s)"
