#pragma once
// The message box (apps/editor/message_box.inc's look, in the kit): a question, an error or a note, with an icon at
// the left, the text wrapped at 420 px (at 100 %) beside it, and the buttons at the bottom right, as a Dialog. The
// first button is the primary one (Enter presses it) and the last is the cancel one (Escape presses it), so an OK
// box is both.
//
// The window's size is estimated from the text before it opens (a view has no canvas then): the text may wrap a
// little differently on screen.

#include <string>
#include <vector>

#include "view.h"

namespace stardraft::kit {

enum class MessageIcon { None, Error, Warning, Question, Info };

// Shows the box from `from` and returns when it closes: the index of the button that closed it (-1 if none).
int messageBox(draw::View& from, const std::wstring& title, const std::wstring& text, const std::vector<std::wstring>& buttons = {L"OK"},
               MessageIcon icon = MessageIcon::None);

}  // namespace stardraft::kit
