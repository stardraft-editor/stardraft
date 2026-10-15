# Types with the system's input methods into a window (CI only: it takes the focus and sends keys).
# usage: ime_keys_windows.ps1 -ProcessId <id> -Keys <sequence>
# <sequence>: keys as typed ("gksrnrdj" is 한국어 on the Korean 2-set layout; a space commits), and in braces:
#   {layout:00000412}  the keyboard layout of the window (Korean; 00000804: Chinese, Microsoft Pinyin)
#   {hangul}           the Hangul key (한/영: Hangul or English letters)
#   {hanja}            the Hanja key (the candidates of the syllable being composed)
#   {enter} {wait}     Enter; half a second
param([int]$ProcessId, [string]$Keys)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Ime {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
    [DllImport("user32.dll")] public static extern IntPtr LoadKeyboardLayoutW([MarshalAs(UnmanagedType.LPWStr)] string id, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr w, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w, IntPtr pid);
    [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
    public static void Key(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(60); keybd_event(vk, 0, 2, UIntPtr.Zero); System.Threading.Thread.Sleep(150); }
}
'@

$w = [IntPtr]::Zero
for ($i = 0; $i -lt 50 -and $w -eq [IntPtr]::Zero; $i++) {
    $proc = Get-Process -Id $ProcessId
    $proc.Refresh()
    $w = $proc.MainWindowHandle
    if ($w -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 200 }
}
if ($w -eq [IntPtr]::Zero) { throw "process $ProcessId has no window" }
[Ime]::SetForegroundWindow($w) | Out-Null
Start-Sleep -Milliseconds 500

function Layout([string]$id) {
    $hkl = [Ime]::LoadKeyboardLayoutW($id, 1)
    [Ime]::PostMessageW($w, 0x0050, [IntPtr]::Zero, $hkl) | Out-Null  # WM_INPUTLANGCHANGEREQUEST
    Start-Sleep -Milliseconds 800
    $now = [Ime]::GetKeyboardLayout([Ime]::GetWindowThreadProcessId($w, [IntPtr]::Zero))
    Write-Output ("keyboard layout of the window: {0:X8}" -f $now.ToInt64())
    Start-Sleep -Milliseconds 2500  # (the input method starts: keys before that are lost)
}

$i = 0
while ($i -lt $Keys.Length) {
    if ($Keys[$i] -eq '{') {
        $end = $Keys.IndexOf('}', $i)
        $token = $Keys.Substring($i + 1, $end - $i - 1)
        $i = $end + 1
        if ($token.StartsWith("layout:")) { Layout $token.Substring(7) }
        elseif ($token -eq "hangul") { [Ime]::Key(0x15); Start-Sleep -Milliseconds 300 }
        elseif ($token -eq "hanja") { [Ime]::Key(0x19); Start-Sleep -Milliseconds 800 }
        elseif ($token -eq "enter") { [Ime]::Key(0x0D) }
        elseif ($token -eq "wait") { Start-Sleep -Milliseconds 500 }
        else { throw "unknown key {$token}" }
        continue
    }
    $ch = [string]$Keys[$i]
    if ($ch -eq ' ') { [Ime]::Key(0x20) } else { [Ime]::Key([byte][char]$ch.ToUpper()) }
    $i++
}
Write-Output "sent '$Keys' to process $ProcessId"
