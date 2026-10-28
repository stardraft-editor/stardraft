#Requires AutoHotkey v2.0
OnError(LogError)
LogError(e, mode) {  ; (an error: into the log, and exit; no message box on the user's screen)
    try FileAppend("ERROR " e.Message " " (e.HasProp("Extra") ? e.Extra : "") " (line " e.Line ")`n", A_ScriptDir "\errors.log", "UTF-8")
    ExitApp(2)
}
#Warn All, StdOut
; Real input into a running SCMDraft (for when the user is away): activates its window, runs steps, puts the mouse back.
; Steps: n:ClassNN (click the middle of that control) | c:x,y (click, screen pixels) | d:x1,y1,x2,y2 (drag) | k:keys (Send syntax) | w:ms (wait)
; usage: AutoHotkey64.exe steps.ahk <pid> <step> [step ...]
pid := A_Args[1]
main := WinExist("ahk_pid " pid " ahk_class SCMDRAFT2")
if !main
    ExitApp(1)
CoordMode("Mouse", "Screen")
MouseGetPos(&mx0, &my0)
WinActivate(main)
WinWaitActive(main, , 5)
Sleep(400)
Loop A_Args.Length - 1 {
    s := A_Args[A_Index + 1], kind := SubStr(s, 1, 1), arg := SubStr(s, 3)
    if (kind = "n") {
        h := ControlGetHwnd(arg, main)
        WinGetPos(&cx, &cy, &cw, &ch, h)
        Click(cx + cw // 2, cy + ch // 2)
        FileAppend("clicked " arg " at " (cx + cw // 2) "," (cy + ch // 2) "`n", A_ScriptDir "\steps.log", "UTF-8")
    } else if (kind = "c") {
        p := StrSplit(arg, ","), Click(p[1], p[2])
    } else if (kind = "d") {
        p := StrSplit(arg, ",")
        MouseMove(p[1], p[2], 0), Sleep(100), Click("Down"), Sleep(100)
        MouseMove(p[3], p[4], 10), Sleep(100), Click("Up")
    } else if (kind = "k") {
        Send(arg)
    } else if (kind = "w") {
        Sleep(Integer(arg))
    }
    Sleep(300)
}
MouseMove(mx0, my0, 0)
ExitApp(0)
