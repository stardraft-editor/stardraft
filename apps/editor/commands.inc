// The Win32 part of the Map workspace's commands: the accelerator table, the check marks and the menu's appended items,
// and the graphics and layer commands. The table itself (addCommand, registerMapCommands) is core/map_commands.inc, and
// the menu model is core/menu_model.inc. Each command keeps its old Win32 id, so the top bar, the self-test and command(id)
// reach it as before.

void appendCommand(HMENU m, const char* id) {
    if (const stardraft::Command* c = gCommands.find(id)) AppendMenuW(m, MF_STRING, UINT_PTR(nativeCommandId(id)), commandMenuText(*c).c_str());
}
void appendCommands(HMENU m, const char* menu) {
    for (const stardraft::Command* c : gCommands.inMenu(menu)) AppendMenuW(m, MF_STRING, UINT_PTR(nativeCommandId(c->id)), commandMenuText(*c).c_str());
}

// The check marks of the commands in the menu bar (a dot for one of a group).
void updateCommandMarks(HMENU m) {
    for (const stardraft::Command& c : gCommands.all()) {
        if (!c.checked) continue;
        const UINT item = UINT(nativeCommandId(c.id));
        if (c.radio) {
            MENUITEMINFOW mi{sizeof mi};
            mi.fMask = MIIM_FTYPE | MIIM_STATE;
            if (!GetMenuItemInfoW(m, item, FALSE, &mi)) continue;
            mi.fType |= MFT_RADIOCHECK;
            mi.fState = (mi.fState & ~MFS_CHECKED) | (c.isChecked() ? MFS_CHECKED : 0);
            SetMenuItemInfoW(m, item, FALSE, &mi);
        } else {
            CheckMenuItem(m, item, MF_BYCOMMAND | (c.isChecked() ? MF_CHECKED : MF_UNCHECKED));
        }
    }
}

// A key ("Ctrl+Shift+S", "V", "Ctrl+Tab") as the accelerator entry of the command `id`; false: no such key here.
bool accelOfChord(const std::string& chord, int id, ACCEL& out) {
    const auto k = stardraft::parseShortcut(chord);
    if (!k) return false;
    WORD vk = 0;
    if (k->key.size() == 1 && (std::isalnum(static_cast<unsigned char>(k->key[0])))) vk = WORD(k->key[0]);
    else if (k->key == ",") vk = VK_OEM_COMMA;
    else if (k->key == "Del") vk = VK_DELETE;
    else if (k->key == "Tab") vk = VK_TAB;
    else if (k->key.size() > 1 && k->key[0] == 'F') vk = WORD(VK_F1 + std::stoi(k->key.substr(1)) - 1);
    if (!vk) return false;  // (a key this shell gives in its own key handling)
    out = {BYTE(FVIRTKEY | (k->ctrl ? FCONTROL : 0) | (k->shift ? FSHIFT : 0) | (k->alt ? FALT : 0)), vk, WORD(id)};
    return true;
}

// The keys of the commands, as accelerator entries.
std::vector<ACCEL> commandAccelerators() {
    std::vector<ACCEL> out;
    for (const stardraft::Command& c : gCommands.all())
        if (ACCEL a; c.accelerator && accelOfChord(c.shortcut, nativeCommandId(c.id), a)) out.push_back(a);
    return out;
}


