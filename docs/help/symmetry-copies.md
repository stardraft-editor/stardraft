# Symmetry copies

Use **Mode > Configure symmetry copies** to choose Mirrors, Rotations or Custom copies. The spatial preview and copy list show enabled and disabled copies. Click a copy to choose its terrain; double-click toggles it. Source is always placed. **Enable all** and **Source only** affect the complete set.

Rotations support 2–16 copies and optional mirrored rotations. Custom copies combine rotations and selected mirrors; **Include combinations** adds their combinations. Turn it off for a direct union, including deliberately incomplete layouts. Existing combinations retain their behavior when the composer opens.

Copies inherit **Default terrain**, or each can use a different ISOM terrain. **Raw tile IDs** expose optional overrides for Rectangle/custom tile brushes. The source brush wins at overlaps. Every placement remains one undo step. Erase uses the same ground at all enabled copies.

Settings follow stable transforms when counts change and remain saved per palette in local map settings; no extra data is written into the map file.

## Sources and credits

Source attribution: Map Studio (MIT), Copyright (c) 2026 ScmDraft Fan Add-on contributors; https://github.com/RustyNovoBland77464/scmdraft-native (e35461dea3b4387b0956feb59ed07ab4ee8d3fa6). License: `licenses/symmetry-copies.txt` in the source repository.

```text
Symmetry copy controls

Map Studio
==========

MIT License

Copyright (c) 2026 ScmDraft Fan Add-on contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

```
