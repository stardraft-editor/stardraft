#Requires AutoHotkey v2.0
#Warn All, StdOut
OnError(LogError)
LogError(e, mode) {  ; (an error: into the log, and exit; no message box on the user's screen)
    try FileAppend("ERROR " e.Message " " (e.HasProp("Extra") ? e.Extra : "") " (line " e.Line ")`n", A_ScriptDir "\errors.log", "UTF-8")
    ExitApp(2)
}
; Opens a map in SCMDraft with the "CM" profile: starts it with the map, picks CM in its Select Profile dialog, waits for
; the map, and writes what it saw (windows, controls, the main window's place) to a log next to this script.
; usage: AutoHotkey64.exe open_cm.ahk <ScmDraft 2.exe> <map>
exe := A_Args[1], mapFile := A_Args[2]
logFile := A_ScriptDir "\open_cm.log"
try FileDelete(logFile)
Log(s) {
    global logFile
    FileAppend(s "`n", logFile, "UTF-8")
}
SplitPath(exe, , &dir)
Run('"' exe '" "' mapFile '"', dir, , &pid)
Log("pid " pid)
if !WinWait("ahk_pid " pid, , 30) {
    Log("no window")
    ExitApp(1)
}
Sleep(1500)
DumpWindows() {
    global pid
    for hwnd in WinGetList("ahk_pid " pid) {
        t := WinGetTitle(hwnd), c := WinGetClass(hwnd)
        Log("window " hwnd " [" c "] " t)
        try for ctl in WinGetControls(hwnd) {
            txt := ""
            try txt := ControlGetText(ctl, hwnd)
            Log("   control " ctl " " SubStr(StrReplace(txt, "`n", " "), 1, 60))
        }
    }
}
DumpWindows()
; The profile dialog: a list or combo box holding "CM", and an OK button.
picked := false
for hwnd in WinGetList("ahk_pid " pid) {
    try for ctl in WinGetControls(hwnd) {
        if !(InStr(ctl, "ListBox") || InStr(ctl, "ComboBox") || InStr(ctl, "SysListView32"))
            continue
        try {
            if InStr(ctl, "SysListView32") {
                items := ListViewGetContent("Col1", ctl, hwnd)
                row := 0
                for i, line in StrSplit(items, "`n")
                    if (Trim(line) = "CM")
                        row := i
                if row {
                    ControlFocus(ctl, hwnd)
                    SendMessage(0x1000 + 43, row - 1, 0, ctl, hwnd)  ; LVM_ENSUREVISIBLE
                    ControlSend("{Home}", ctl, hwnd)
                    Loop row - 1
                        ControlSend("{Down}", ctl, hwnd)
                    picked := true
                }
            } else {
                ControlChooseString("CM", ctl, hwnd)
                picked := true
            }
        }
        if picked {
            Log("picked CM in " ctl)
            for c2 in WinGetControls(hwnd)
                if InStr(c2, "Button") && (ControlGetText(c2, hwnd) ~= "i)^(&?OK|&?Open|&?Load|&?Select|&?Launch)$") {
                    ControlClick(c2, hwnd)
                    Log("clicked " c2 " " ControlGetText(c2, hwnd))
                    break
                }
            break 2
        }
    }
}
if !picked
    Log("no profile list found")
Sleep(8000)
Log("--- after")
DumpWindows()
main := 0
for hwnd in WinGetList("ahk_pid " pid)
    if InStr(WinGetTitle(hwnd), ".scx")
        main := hwnd
if main {
    ; (the window as it is: no focus taken)
    Sleep(2500)
    WinGetPos(&x, &y, &w, &h, main)
    Log("main " x " " y " " w " " h)
} else
    Log("no map window")
ExitApp(0)
