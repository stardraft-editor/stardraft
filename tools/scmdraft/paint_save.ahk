#Requires AutoHotkey v2.0
OnError(LogError)
LogError(e, mode) {  ; (an error: into the log, and exit; no message box on the user's screen)
    try FileAppend("ERROR " e.Message " " (e.HasProp("Extra") ? e.Extra : "") " (line " e.Line ")`n", A_ScriptDir "\errors.log", "UTF-8")
    ExitApp(2)
}
#Warn All, StdOut  ; (warnings to the console: no message boxes on the user's screen)
; In a running SCMDraft (its pid): picks a terrain in the Terrain palette tree by name, clicks the map view at points
; (in the map view's client pixels, by messages: the mouse and the focus stay the user's) to paint with the isometric brush, then saves the map as <out> (File > Save As).
; usage: AutoHotkey64.exe paint_save.ahk <pid> <terrain name> <out .scx> <x,y> [x,y ...]
pid := A_Args[1], terrain := A_Args[2], outFile := A_Args[3]
logFile := A_ScriptDir "\paint_save.log"
try FileDelete(logFile)
Log(s) {
    global logFile
    FileAppend(s "`n", logFile, "UTF-8")
}
main := WinExist("ahk_pid " pid " ahk_class SCMDRAFT2")
if !main {
    Log("no SCMDraft window")
    ExitApp(1)
}
; The terrain tree: the SysTreeView32 that holds the terrain names.
tree := ""
for ctl in WinGetControls(main)
    if InStr(ctl, "SysTreeView32")
        tree := ctl
Log("tree " tree)
; Select the item by name: TVM_GETNEXTITEM walk with TVM_GETITEM text (in SCMDraft's process memory).
TV_FindAndSelect(ctlHwnd, name) {
    static TVM_GETNEXTITEM := 0x110A, TVM_SELECTITEM := 0x110B, TVM_GETITEMW := 0x113E, TVM_EXPAND := 0x1102
    pidT := WinGetPID(ctlHwnd)
    hProc := DllCall("OpenProcess", "UInt", 0x438, "Int", 0, "UInt", pidT, "Ptr")
    remote := DllCall("VirtualAllocEx", "Ptr", hProc, "Ptr", 0, "UPtr", 4096, "UInt", 0x3000, "UInt", 4, "Ptr")
    item := SendMessage(TVM_GETNEXTITEM, 0, 0, ctlHwnd)  ; root
    found := 0
    stack := [item]
    while stack.Length && !found {
        it := stack.Pop()
        while it {
            SendMessage(TVM_EXPAND, 2, it, ctlHwnd)  ; TVE_EXPAND
            tv := Buffer(40, 0)  ; (TVITEMW as a 32-bit program has it: SCMDraft is one)
            NumPut("UInt", 0x1 | 0x10, tv, 0)  ; TVIF_TEXT | TVIF_HANDLE
            NumPut("UInt", it, tv, 4)
            NumPut("UInt", remote + 512, tv, 16)
            NumPut("Int", 255, tv, 20)
            DllCall("WriteProcessMemory", "Ptr", hProc, "Ptr", remote, "Ptr", tv, "UPtr", tv.Size, "Ptr", 0)
            SendMessage(TVM_GETITEMW, 0, remote, ctlHwnd)
            txt := Buffer(512, 0)
            DllCall("ReadProcessMemory", "Ptr", hProc, "Ptr", remote + 512, "Ptr", txt, "UPtr", 510, "Ptr", 0)
            if (StrGet(txt, "UTF-16") = name) {
                found := it
                break
            }
            child := SendMessage(TVM_GETNEXTITEM, 4, it, ctlHwnd)  ; TVGN_CHILD
            if child
                stack.Push(child)
            it := SendMessage(TVM_GETNEXTITEM, 1, it, ctlHwnd)  ; TVGN_NEXT
        }
    }
    if found
        SendMessage(TVM_SELECTITEM, 9, found, ctlHwnd)  ; TVGN_CARET
    DllCall("VirtualFreeEx", "Ptr", hProc, "Ptr", remote, "UPtr", 0, "UInt", 0x8000)
    DllCall("CloseHandle", "Ptr", hProc)
    return found
}
th := ControlGetHwnd(tree, main)
f := TV_FindAndSelect(th, terrain)
Log("terrain item " f)
if !f
    ExitApp(1)
; The map view.
view := ""
for ctl in WinGetControls(main)
    if InStr(ctl, "ScmDraft Map Window")
        view := ctl
vh := ControlGetHwnd(view, main)
WinGetPos(&vx, &vy, &vw, &vhh, vh)
Log("view " vx " " vy " " vw " " vhh)
Loop A_Args.Length - 3 {  ; (messages to the map view: no mouse, no focus)
    pt := StrSplit(A_Args[A_Index + 3], ",")
    ControlClick("x" pt[1] " y" pt[2], vh, , "Left", 1, "NA")
    Sleep(400)
    Log("clicked " pt[1] "," pt[2])
}
Sleep(800)
; File > Save As: the common dialog, the file name, Enter.
; (Save As in SCMDraft's menus: the first menu may be a maximized map window's own; the item's command, sent)
hMenu := DllCall("GetMenu", "Ptr", main, "Ptr"), saveId := 0
Loop DllCall("GetMenuItemCount", "Ptr", hMenu, "Int") {
    hSub := DllCall("GetSubMenu", "Ptr", hMenu, "Int", A_Index - 1, "Ptr")
    if !hSub
        continue
    Loop DllCall("GetMenuItemCount", "Ptr", hSub, "Int") {
        buf := Buffer(512, 0)
        DllCall("GetMenuStringW", "Ptr", hSub, "UInt", A_Index - 1, "Ptr", buf, "Int", 255, "UInt", 0x400)
        if !saveId && InStr(StrReplace(StrGet(buf, "UTF-16"), "&"), "Save As") {
            saveId := DllCall("GetMenuItemID", "Ptr", hSub, "Int", A_Index - 1, "UInt")
            Log("save as: " StrGet(buf, "UTF-16") " (" saveId ")")
        }
    }
}
if !saveId {
    Log("no Save As item")
    ExitApp(1)
}
PostMessage(0x111, saveId, 0, , main)  ; WM_COMMAND
if WinWait("ahk_class #32770 ahk_pid " pid, , 10) {
    dlg := WinExist()
    Sleep(500)
    ControlSetText(outFile, "Edit1", dlg)
    ControlSend("{Enter}", "Edit1", dlg)
    Sleep(1500)
    if WinExist("ahk_class #32770 ahk_pid " pid)  ; (overwrite or format questions: yes)
        ControlSend("{Enter}", , "ahk_class #32770 ahk_pid " pid)
    Sleep(1500)
    Log("saved " FileExist(outFile))
} else
    Log("no save dialog")
ExitApp(0)
