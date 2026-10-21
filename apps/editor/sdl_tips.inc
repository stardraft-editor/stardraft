// The hover tooltips on this system: the views' tips (core/views_tip.inc: HoverTipper, told by the host where the
// mouse is). Included by main_sdl.cpp after sdl_data.inc.

// The hover tooltips of every window (SdlHost::setHoverHook calls it).
HoverTipper gHoverTips;
void sdlHover(draw::View& root, draw::Point pt, const std::wstring& tip, bool reset) {
    if (reset) gHoverTips.reset(root);
    else gHoverTips.moved(root, pt, tip, simTipIsQuick(tip) ? 80 : -1);
}
