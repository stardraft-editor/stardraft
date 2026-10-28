#Requires AutoHotkey v2.0
OnError(LogError)
LogError(e, mode) {
    try FileAppend("ERROR " e.Message " (line " e.Line ")`n", A_ScriptDir "\errors.log", "UTF-8")
    ExitApp(2)
}
; SCMDraft: File > Save As (its command, 32904) to <out>, answering its questions with Enter; the result into save.log.
pid := A_Args[1], outFile := A_Args[2]
main := WinExist("ahk_pid " pid " ahk_class SCMDRAFT2")
PostMessage(0x111, 32904, 0, , main)
if !WinWait("Save ahk_class #32770 ahk_pid " pid, , 10) {
    FileAppend("no save dialog`n", A_ScriptDir "\save.log", "UTF-8")
    ExitApp(1)
}
dlg := WinExist()
Sleep(500)
ControlSetText(outFile, "Edit1", dlg)
ControlSend("{Enter}", "Edit1", dlg)
Sleep(2000)
if WinExist("ahk_class #32770 ahk_pid " pid) && WinGetTitle() != "Ash World terrain"
    ControlSend("{Enter}", , "ahk_id " WinExist())
Sleep(2000)
FileAppend("saved " FileExist(outFile) "`n", A_ScriptDir "\save.log", "UTF-8")
