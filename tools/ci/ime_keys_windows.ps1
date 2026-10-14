# Types Korean with the Microsoft Korean IME into a window (CI only: it takes the focus and sends keys).
# usage: ime_keys_windows.ps1 <process id> <keys>
# <keys>: the keys of the Korean 2-set layout (dubeolsik), "gksrnrdj" for 한국어; a space at the end commits.
param([int]$ProcessId, [string]$Keys)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Ime {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
    [DllImport("user32.dll")] public static extern IntPtr LoadKeyboardLayoutW([MarshalAs(UnmanagedType.LPWStr)] string id, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr w, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("imm32.dll")] public static extern IntPtr ImmGetContext(IntPtr w);
    [DllImport("imm32.dll")] public static extern bool ImmSetOpenStatus(IntPtr c, bool open);
    [DllImport("imm32.dll")] public static extern bool ImmSetConversionStatus(IntPtr c, uint conv, uint sentence);
    [DllImport("imm32.dll")] public static extern bool ImmReleaseContext(IntPtr w, IntPtr c);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w, IntPtr pid);
    [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
    public static void Key(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(60); keybd_event(vk, 0, 2, UIntPtr.Zero); System.Threading.Thread.Sleep(120); }
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
Write-Output "window $w"
[Ime]::SetForegroundWindow($w) | Out-Null
Start-Sleep -Milliseconds 500
# The Korean layout for the window (WM_INPUTLANGCHANGEREQUEST), then the IME open in Hangul mode.
$hkl = [Ime]::LoadKeyboardLayoutW("00000412", 1)
[Ime]::PostMessageW($w, 0x0050, [IntPtr]::Zero, $hkl) | Out-Null
Start-Sleep -Milliseconds 800
$layout = [Ime]::GetKeyboardLayout([Ime]::GetWindowThreadProcessId($w, [IntPtr]::Zero))
Write-Output ("keyboard layout of the window: {0:X8}" -f $layout.ToInt64())
# The Hangul key (VK_HANGUL) turns Hangul input on (the IME starts in English letters).
[Ime]::Key(0x15)
Start-Sleep -Milliseconds 500
foreach ($ch in $Keys.ToCharArray()) {
    if ($ch -eq ' ') { [Ime]::Key(0x20) } else { [Ime]::Key([byte][char]([string]$ch).ToUpper()) }
}
Write-Output "sent $($Keys.Length) keys to process $ProcessId"
