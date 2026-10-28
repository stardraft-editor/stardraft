#Requires AutoHotkey v2.0
; SCMDraft's menus: each item's text and command id, into menus.log (reads only; no input).
main := WinExist("ahk_pid " A_Args[1] " ahk_class SCMDRAFT2")
out := ""
Dump(h, depth) {
    global out
    Loop DllCall("GetMenuItemCount", "Ptr", h, "Int") {
        buf := Buffer(512, 0)
        DllCall("GetMenuStringW", "Ptr", h, "UInt", A_Index - 1, "Ptr", buf, "Int", 255, "UInt", 0x400)
        id := DllCall("GetMenuItemID", "Ptr", h, "Int", A_Index - 1, "Int")
        out .= Format("{:" depth * 2 + 1 "}", "") StrGet(buf, "UTF-16") " (" id ")`n"
        sub := DllCall("GetSubMenu", "Ptr", h, "Int", A_Index - 1, "Ptr")
        if sub
            Dump(sub, depth + 1)
    }
}
Dump(DllCall("GetMenu", "Ptr", main, "Ptr"), 0)
FileAppend(out, A_ScriptDir "\menus.log", "UTF-8")
