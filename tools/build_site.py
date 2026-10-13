"""Builds the static website (docs/index.html, served by GitHub Pages) from docs/project/ROADMAP.md.

ROADMAP.md is the source. The page shows its "## Now", "## Next" and "## Later" sections. An item is
"- [ ] **R<n> Title.** *Tag:* area." and then, indented, a short paragraph that tells what the feature is.
The page shows the title and that paragraph. The paragraphs after it are only in ROADMAP.md.

    python tools/build_site.py          write docs/index.html and docs/assets
    python tools/build_site.py --check  exit 1 if they are out of date

The page is in English and Korean: a script in it swaps each text (data-i18n) for its Korean from KO, KO_PERSONAS,
KO_INTROS, KO_TAGS and KO_ROADMAP below, by the browser's language or the EN | KR switch. A roadmap item without Korean
stays in English, and the build says which ones.

The screenshots in docs/assets/shots are datashots (not made here), all in a 2100 x 1300 window and cut to the same
1890 x 1260 box (x 8 or 202, y 30), as WebP. The Data workspace needs this width: narrower, its pages go to one column.
Program options > Language must be English for them (it is read even in test runs), except korean.webp. Set it
back to the value it had before.
    stardraft-testing.exe <map> --datashots <dir> rpgdemo:   (makes the AshenCrown project)
    stardraft-testing.exe "(2)Crescent Dyne.scm" --datashots <dir> winsize:2100x1300 view:hd cam:1900:2600:0.7 ws:map
        new:generate
    stardraft-testing.exe "Cave Wars v012.scx" --datashots <dir> winsize:2100x1300 view:hd cam:3067:3058:0.7 ws:map
        quick:units:protoss  (quick.webp: the picker pasted over the map, with a soft shadow)
    stardraft-testing.exe <dir>/AshenCrown/AshenCrown.eds --datashots <dir> winsize:2100x1300 trig:2:2 ws:data units:20
        weapons:0 buttons:51 images:239
    stardraft-testing.exe "(1)Underdogs03.scx" --datashots <dir> winsize:2100x1300 brec:0:40000  (frame f00220.png)
    In Korean: AshenCrown.eds ... trig:2:2 options:  (korean.webp: options_dark.png over the dimmed triggers page)
"""
import html
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
ROADMAP = ROOT / "docs" / "project" / "ROADMAP.md"
OUT = ROOT / "docs" / "index.html"
ICONS = ROOT / "tools" / "icongen" / "out"
REPO = "https://github.com/stardraft-editor/stardraft"
DISCORD = "https://discord.gg/JBMCwSRJBm"
FEEDBACK = "https://discord.com/channels/1558446827391418458/1559379661111304262"  # the #feedback forum
VIDEO = "pUr3eruPKA4"

# The kinds of projects (and the groups that make them): id, name, UI icon, the features it gets.
PERSONAS = [
    ("melee", "Melee map makers", "terrain",
     "Paint terrain with brushes, spray, fill, replace and symmetry, or generate a full map. See the town area of "
     "each base, in HD, SD or Carbot graphics."),
    ("ums", "UMS & campaign makers", "flag",
     "A modern classic trigger editor, unit, upgrade and technology settings, and mission briefings with preview. "
     "Campaign projects are coming soon."),
    ("eud", "EUD map makers", "triggers",
     "Edit unit, weapon, image and sprite data, and write triggers as blocks in plain sentences, or in your own "
     "epScript files. Checks find EUD errors and desyncs before you build."),
    ("mod", "Mod makers", "molecule",
     "Integrated mod editing: make maps with your custom mod files, and manage them in one place. For mods for "
     "both StarCraft: Remastered and 1.16.1."),
]
BUCKETS = ["Now", "Next", "Later"]

# The page's texts. The English ones are the source; KO has the same keys. The roadmap's English is ROADMAP.md; its
# Korean is in KO_ROADMAP (by item id: the title and the summary), KO_INTROS and KO_TAGS.
_ISSUES = REPO + "/issues"
EN = {
    "description": "StarDraft 2 is a StarCraft creation suite for map and mod creators, and the spiritual successor to "
                   "StarDraft by Camelot Systems.",
    "lede": "A StarCraft creation suite for map and mod creators. Built by the community, for the community.",
    "heritage": "StarDraft 2 aims to be the spiritual successor to StarDraft, the 1998 editor by Camelot Systems that "
                "powered the StarCraft modding community of the early 2000s.",
    "download": "Download for Windows", "discord": "Join the Discord", "builds": "Mac and Linux builds coming soon",
    "shots": "Screenshots", "shot": "Screenshot", "prev": "Previous screenshot", "next": "Next screenshot",
    "close": "Close", "of": "{k} of {n}",
    "cap1": "Map editing in HD, SD or Carbot graphics",
    "cap2": "Quick search: press Q to find any unit or tile",
    "cap3": "Terrain generation from a tileset, amounts and shape",
    "cap4": "EUD triggers as blocks, with the epScript beside them",
    "cap5": "Unit editing with a live range preview",
    "cap6": "Weapon editing: damage for each size, upgrades and range",
    "cap7": "Button set editing with a live command card",
    "cap8": "iScript player for every unit animation",
    "cap9": "Briefing editing with a timeline and preview",
    "cap10": "Multi-language support",
    "alt1": "The Map workspace in HD graphics: the jungle map Crescent Dyne",
    "alt2": "The quick picker over the map Cave Wars: protoss typed, and the matching units",
    "alt3": "The new map page: a generated jungle map with sliders for each terrain and the shape",
    "alt4": "The Triggers workspace: a hero power on the Q key as blocks, and its epScript",
    "alt5": "The Data workspace: Jim Raynor with changed hit points and armor, and his weapon ranges",
    "alt6": "A weapon in the Data workspace: its damage against each unit size and upgrade, and its range",
    "alt7": "Infested Kerrigan's button set: its buttons, their command card and the selected button's fields",
    "alt8": "The image player: the marine's animations, played frame by frame",
    "alt9": "The Briefing workspace of Underdogs: Kerrigan and Raynor talk in the preview, beside the briefing actions",
    "alt10": "Program options in Korean, over the Triggers workspace in Korean",
    "who": "Who it is for", "video": "StarDraft 2 video", "roadmap": "Roadmap",
    "sourced": f'Sourced from <a href="{_ISSUES}">user reports</a> and <a href="{FEEDBACK}">feedback</a>. Frequent releases are planned.',
    "Now": "Now", "Next": "Next", "Later": "Later", "empty": "Nothing listed yet.", "link": "Link to this item",
    "soon": "Coming soon", "feedback": "Feedback",
    "way1": "In StarDraft 2", "way1text": "Right-click the part that is wrong and select <em>Report feedback on &hellip;</em>",
    "way2": "On Discord", "way2text": f'Post in the <a href="{FEEDBACK}">#feedback</a> channel.',
    "way3": "On GitHub", "way3text": f'<a href="{_ISSUES}">Open an issue</a>.',
    "changelog": "Changelog", "report": "Report a problem",
}
KO = {
    "description": "StarDraft 2는 맵·모드 제작자를 위한 스타크래프트 제작 도구 모음이며, Camelot Systems의 StarDraft를 잇는 "
                   "정신적 후속작입니다.",
    "lede": "맵·모드 제작자를 위한 스타크래프트 제작 도구 모음. 커뮤니티가, 커뮤니티를 위해 만듭니다.",
    "heritage": "StarDraft 2는 2000년대 초 스타크래프트 모딩 커뮤니티를 이끈 1998년 Camelot Systems의 에디터, StarDraft의 "
                "정신적 후속작을 목표로 합니다.",
    "download": "Windows용 다운로드", "discord": "Discord 참여하기", "builds": "Mac·Linux 버전 출시 예정",
    "shots": "스크린샷", "shot": "스크린샷", "prev": "이전 스크린샷", "next": "다음 스크린샷", "close": "닫기",
    "of": "{n}장 중 {k}",
    "cap1": "HD·SD·Carbot 그래픽으로 맵 편집",
    "cap2": "빠른 검색: Q를 눌러 유닛이나 타일 찾기",
    "cap3": "타일셋, 비율, 모양으로 지형 생성",
    "cap4": "블록으로 만드는 EUD 트리거와 바로 옆의 epScript",
    "cap5": "실시간 사거리 미리보기와 함께하는 유닛 편집",
    "cap6": "무기 편집: 크기별 피해, 업그레이드, 사거리",
    "cap7": "실시간 명령 카드와 함께하는 버튼 세트 편집",
    "cap8": "모든 유닛 애니메이션을 재생하는 iScript 플레이어",
    "cap9": "타임라인과 미리보기로 브리핑 편집",
    "cap10": "다국어 지원",
    "alt1": "HD 그래픽의 맵 작업 공간: 정글 맵 Crescent Dyne",
    "alt2": "Cave Wars 맵 위의 빠른 검색: protoss를 입력하고 일치하는 유닛이 표시됨",
    "alt3": "새 맵 페이지: 생성된 정글 맵과 지형별 비율, 모양 슬라이더",
    "alt4": "트리거 작업 공간: Q 키로 쓰는 영웅 능력 블록과 그 epScript",
    "alt5": "데이터 작업 공간: 체력과 방어력을 바꾼 짐 레이너와 무기 사거리",
    "alt6": "데이터 작업 공간의 무기: 유닛 크기와 업그레이드별 피해, 사거리",
    "alt7": "감염된 케리건의 버튼 세트: 버튼, 명령 카드, 선택한 버튼의 항목",
    "alt8": "이미지 플레이어: 마린의 애니메이션을 프레임 단위로 재생",
    "alt9": "Underdogs의 브리핑 작업 공간: 미리보기에서 케리건과 레이너가 대화하고, 옆에 브리핑 액션이 있음",
    "alt10": "한국어로 된 프로그램 옵션과 한국어 트리거 작업 공간",
    "who": "이런 분들을 위해", "video": "StarDraft 2 영상", "roadmap": "로드맵",
    "sourced": f'사용자 <a href="{_ISSUES}">제보</a>와 <a href="{FEEDBACK}">피드백</a>을 바탕으로 정합니다. 자주 릴리스할 예정입니다.',
    "Now": "지금", "Next": "다음", "Later": "나중에", "empty": "아직 없습니다.", "link": "이 항목 링크",
    "soon": "출시 예정", "feedback": "피드백",
    "way1": "StarDraft 2에서",
    "way1text": "잘못된 부분을 마우스 오른쪽 버튼으로 클릭하고 <em>피드백 보고: &hellip;</em>를 선택하세요.",
    "way2": "Discord에서", "way2text": f'<a href="{FEEDBACK}">#feedback</a> 채널에 글을 남겨 주세요.',
    "way3": "GitHub에서", "way3text": f'<a href="{_ISSUES}">이슈를 여세요</a>.',
    "changelog": "변경 내역", "report": "문제 신고",
}
KO_PERSONAS = {
    "melee": ("밀리 맵 제작자", "브러시, 스프레이, 채우기, 바꾸기, 대칭으로 지형을 칠하거나 맵 전체를 생성하세요. 각 기지의 "
                              "타운 영역을 HD, SD, Carbot 그래픽으로 확인할 수 있습니다."),
    "ums": ("유즈맵·캠페인 제작자", "현대적인 클래식 트리거 에디터, 유닛·업그레이드·기술 설정, 미리보기가 있는 미션 브리핑. "
                                 "캠페인 프로젝트는 곧 지원됩니다."),
    "eud": ("EUD 맵 제작자", "유닛, 무기, 이미지, 스프라이트 데이터를 편집하고, 트리거를 평범한 문장으로 된 블록이나 직접 만든 "
                            "epScript 파일로 작성하세요. 빌드하기 전에 EUD 오류와 디싱크를 검사합니다."),
    "mod": ("모드 제작자", "통합 모드 편집: 커스텀 모드 파일로 맵을 만들고 한곳에서 관리하세요. 스타크래프트: 리마스터와 1.16.1 "
                         "모드를 모두 지원합니다."),
}
KO_INTROS = {
    "Now": "빠른 개선, 그리고 초기 사용자 피드백이 요청한 부족한 기능.",
    "Next": "StarDraft 2를 더 강력한 제작 도구로 만듭니다.",
    "Later": "스타크래프트 창작의 다음 진화를 가능하게 할 방법을 탐구합니다.",
}
KO_TAGS = {"Terrain": "지형", "UMS": "유즈맵", "Capability": "기능", "Mods": "모드", "Platform": "플랫폼"}
KO_ROADMAP = {
    "R36": ("클릭할 수 있는 빌드 오류", "로그의 빌드 오류를 클릭하면 트리거 작업 공간에서 그 트리거와 블록이 열립니다."),
    "R39": ("맵 그래픽으로 보는 타일", "타일 목록이 각 타일을 맵의 그래픽(HD, SD, Carbot)으로 보여 줍니다."),
    "R8": ("미리 배치된 유닛의 그래픽", "미리 배치된 유닛이 맵의 데이터 편집에 따른 그래픽으로 표시됩니다."),
    "R2": ("지형 렌더링 정확도", "모든 타일셋이 HD, SD, Carbot에서 게임과 똑같이 보입니다."),
    "R50": ("더 많은 EUD 트리거 블록", "자주 쓰는 맵 작업을 위한 블록: 화면 중앙 맞추기, 건물 대기열, 애니메이션 재생, "
                                     "스프라이트 배치, 스프라이트 효과."),
    "R37": ("지형 장식 도구", "지형에 장식물과 장식을 놓는 도구. 예를 들어 맞는 곳에만 장식물을 뿌리는 장식물 스프레이."),
    "R40": ("떠 있는 팔레트", "각 팔레트(지형, 타일, 유닛, 장식물, 스프라이트)를 떠 있는 창으로 엽니다. 여러 개를 함께 열고 "
                            "각각 다른 필터를 쓸 수 있습니다."),
    "R38": ("검사기 문서", "EUD·디싱크 검사마다 도움말이 있습니다: 규칙, 예시, 검사가 찾지 못하는 것. 트리거 작업 공간의 각 "
                         "표시에서 도움말로 연결됩니다."),
    "R33": ("유닛 사거리 오버레이", "선택한 유닛의 무기 사거리, 시야, 탐지 범위, 사이오닉 영역을 맵에 표시합니다."),
    "R34": ("지형 플래그 오버레이", "어느 지형을 걸을 수 있는지, 얼마나 높은지, 어느 지형이 엄폐를 주거나 시야를 막는지 맵에 "
                                 "표시합니다."),
    "R32": ("전장의 안개 미리보기", "플레이어를 선택하면 게임을 시작할 때 그 플레이어의 전장의 안개를 볼 수 있습니다."),
    "R14": ("정확한 타이머", "긴 게임에서도 정확한 \"N초 후\"와 \"N초마다\" 블록."),
    "R23": ("트리거 일괄 생성", "패턴 하나로 트리거를 여러 개 만듭니다: 플레이어, 로케이션, 유닛 종류마다 트리거 하나씩."),
    "R25": ("캠페인 프로젝트", "캠페인의 모든 맵을 순서대로 프로젝트 하나에 둡니다."),
    "R35": ("길찾기 영역 오버레이", "유닛이 길을 찾을 때 쓰는 영역을 보여 줍니다. 유닛이 왜 그 길로 가거나 막히는지 알 수 "
                                 "있습니다."),
    "R31": ("리플레이 미리보기", "컴퓨터 플레이어로 맵의 리플레이를 만들어 스타크래프트에서 봅니다."),
    "R42": ("scmscx.com에서 맵 가져오기", "[scmscx.com](https://scmscx.com)에서 맵을 찾아 StarDraft 2에서 엽니다."),
    "R49": ("새로워진 지형 생성", "웨이브 함수 붕괴(wave function collapse) 같은 제약 기반 방법으로 맵을 생성합니다: 지형 "
                               "규칙을 정하면 그 규칙을 따르는 맵이 나옵니다."),
    "R51": ("고급 EUD 기능", "큐와 덱(메서드와 반복문 포함), 바이트 읽기·쓰기, 채팅 명령을 위한 텍스트 비교·검색, 텍스트를 "
                           "숫자로 바꾸기, 그리기, 배경 음악을 위한 블록."),
    "R26": ("리마스터 모드 지원", "스타크래프트: 리마스터 모드(MPQ 또는 폴더)를 게임 파일 위에 불러와 맵을 만듭니다."),
    "R41": ("전투 시뮬레이터", "유닛 수와 업그레이드를 정해 유닛끼리의 전투를 시뮬레이션합니다. 누가 이기는지, 남은 체력, "
                             "전투 시간을 볼 수 있습니다."),
    "R45": ("맵 게시", "StarDraft 2에서 [scmscx.com](https://scmscx.com)에 맵을 올립니다."),
    "R46": ("1.16.1 모드 지원", "[Cosmonarchy](https://cosmonarchy.com/) 같은 스타크래프트 1.16.1 모드를 불러와 맵을 "
                              "만듭니다."),
    "R47": ("버전 기록", "맵이나 프로젝트의 두 버전을 비교하고 변경 사항을 합칩니다."),
    "R48": ("맵 템플릿", "디펜스 맵이나 RPG 같은 템플릿에서 레이아웃과 트리거가 갖춰진 맵을 시작합니다."),
    "R43": ("공동 편집", "두 명 이상이 같은 맵이나 프로젝트를 동시에 편집합니다."),
    "R28": ("macOS 버전", "macOS용 StarDraft 2."),
    "R29": ("Linux 버전", "Linux용 StarDraft 2."),
    "R30": ("웹 버전", "설치 없이 브라우저에서 쓰는 StarDraft 2."),
}


def parse(text):
    """The items, and the sentence under each section heading."""
    items, intros, bucket = [], {}, None
    for block in re.split(r"\n(?=## |- \[)", text):
        h = re.match(r"## (\w+)(.*)", block, re.S)
        if h:
            bucket = h.group(1)
            intros[bucket] = " ".join(h.group(2).split())
            continue
        m = re.match(r"- \[ \] \*\*(R\d+) (.+?)\.?\*\*(.*)\n((?:  \S.*\n?)+)", block)
        if not m or bucket not in BUCKETS:  # done items ("- [x]") are not shown
            continue
        tag = re.sub(r"^\s*\*Tag:\*\s*", "", m.group(3)).strip().rstrip(".")
        tags = [x.strip() for x in tag.split(",") if x.strip()]
        items.append({"id": m.group(1), "title": m.group(2), "text": " ".join(m.group(4).split()),
                      "bucket": bucket, "tags": tags})
    return items, intros


# Icons that only the website uses, in the style of the UI icons (16 px box, lines of weight 1 on .5).
_LINE = 'fill="none" stroke="#fff" stroke-width="1" stroke-linecap="round" stroke-linejoin="round"'
SITE_ICONS = {
    "flag": f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">'
            f'<path d="M3.5 1.5V14.5" {_LINE}/><path d="M3.5 2.5H13L10.5 5.5L13 8.5H3.5" {_LINE}/></svg>',
    "molecule": f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">'
                f'<circle cx="8" cy="4" r="2" {_LINE}/><circle cx="4" cy="12" r="2" {_LINE}/>'
                f'<circle cx="12" cy="12" r="2" {_LINE}/>'
                f'<path d="M7.1 5.8L4.9 10.2M8.9 5.8L11.1 10.2M6 12H10" {_LINE}/></svg>',
}


def icon(name):
    """A UI icon (tools/icongen/out/ui/theme, or SITE_ICONS) as inline SVG in the text colour."""
    svg = SITE_ICONS.get(name) or (ICONS / "ui" / "theme" / f"{name}.svg").read_text(encoding="utf-8")
    svg = svg.replace('<rect width="16" height="16" fill="#000"/>', "").replace("#fff", "currentColor")
    return svg.replace('width="16" height="16"', 'class="icon" overflow="visible" aria-hidden="true"', 1)


def inline(s):
    s = html.escape(s, quote=False)
    s = re.sub(r"\[(.+?)\]\((https://[^)\s]+)\)", r'<a href="\2">\1</a>', s)
    s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
    return re.sub(r"\*(.+?)\*", r"<em>\1</em>", s)


def item_html(i):
    rid = f'R{int(i["id"][1:]):04}'  # R36 shows as R0036
    anchor = rid.lower()
    # The last line: the id, which links to the item (to copy a link to it), then the tags.
    meta = " · ".join([f'<a class="rid" href="#{anchor}" title="{EN["link"]}" data-i18n-title="link">#{rid}</a>'] +
                      [f'<span data-i18n="tag.{tag}">{html.escape(tag)}</span>' for tag in i["tags"]])
    k = i["id"]
    return (f'        <li class="item" id="{anchor}"><h4 data-i18n="{k}.t">{inline(i["title"])}</h4>'
            f'<p data-i18n="{k}.p">{inline(i["text"])}</p><p class="who">{meta}</p></li>')


def korean(items):
    """The Korean of each data-i18n key in the page (inline Markdown already made HTML)."""
    ko = dict(KO, **{f"of{k}": KO["of"].format(k=k, n=10) for k in range(1, 11)})
    for pid, (n, text) in KO_PERSONAS.items():
        ko[f"persona.{pid}.n"], ko[f"persona.{pid}.t"] = html.escape(n), html.escape(text)
    for name, text in KO_INTROS.items():
        ko[f"intro.{name}"] = inline(text)
    for tag, text in KO_TAGS.items():
        ko[f"tag.{tag}"] = html.escape(text)
    for i in items:
        if i["id"] in KO_ROADMAP:
            title, text = KO_ROADMAP[i["id"]]
            ko[i["id"] + ".t"], ko[i["id"] + ".p"] = inline(title), inline(text)
    return ko


def build(items, intros):
    L = dict(EN, **{f"of{k}": EN["of"].format(k=k, n=10) for k in range(1, 11)})
    personas = "\n".join(
        f'      <div class="persona{" soon" if pid == "mod" else ""}"><span class="tile">{icon(ic)}</span>'
        f'<h3 data-i18n="persona.{pid}.n">{html.escape(n)}</h3><p data-i18n="persona.{pid}.t">{html.escape(text)}</p>'
        f'{"<p class=badge data-i18n=soon>" + L["soon"] + "</p>" if pid == "mod" else ""}</div>'
        for pid, n, ic, text in PERSONAS)
    buckets = []
    for name in BUCKETS:
        cards = "\n".join(item_html(i) for i in items if i["bucket"] == name)
        body = (f'<ul class="items">\n{cards}\n      </ul>' if cards
                else f'<p class="empty" data-i18n="empty">{L["empty"]}</p>')
        intro = f'<p data-i18n="intro.{name}">{inline(intros[name])}</p>' if intros.get(name) else ""
        buckets.append(f'      <div class="bucket" id="{name.lower()}">\n        <div class="rail">'
                       f'<h3 data-i18n="{name}">{L[name]}</h3>{intro}</div>\n      {body}\n      </div>')
    ko_json = json.dumps(korean(items), ensure_ascii=False, indent=1).replace("</", "<\\/")
    return TEMPLATE.format(personas=personas, buckets="\n".join(buckets), repo=REPO, discord=DISCORD, feedback=FEEDBACK,
                           video=VIDEO, i_select=icon("select"), i_feedback=icon("feedback"), i_issue=icon("issue"), L=L,
                           ko_json=ko_json)


TEMPLATE = """<!doctype html>
<!-- Generated by tools/build_site.py from docs/project/ROADMAP.md (Korean: the KO tables in build_site.py). Do not edit. -->
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>StarDraft 2</title>
<meta name="description" content="{L[description]}" data-i18n-content="description">
<meta name="color-scheme" content="dark">
<link rel="icon" type="image/png" sizes="32x32" href="assets/app-32.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500;600&family=Noto+Sans+KR:wght@400;500;600&display=swap">
<style>
:root {{
  --bg: #0c0e11; --raise: #13161a; --line: #252a31; --grid: rgba(92, 224, 150, 0.06);
  --text: #e9e5dc; --muted: #8e939b; --accent: #5ce096; --on-accent: #04130a;
  --display: "Chakra Petch", "Noto Sans KR", system-ui, sans-serif;
  --sans: "IBM Plex Sans", "Noto Sans KR", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, Consolas, monospace;
}}
* {{ box-sizing: border-box; }}
html {{ scroll-behavior: smooth; }}
body {{ margin: 0; background: var(--bg); color: var(--text); font: 16px/1.6 var(--sans);
       -webkit-font-smoothing: antialiased; }}
.wrap {{ max-width: 1280px; margin: 0 auto; padding: 0 16px; }}
@media (min-width: 700px) {{ .wrap {{ padding: 0 32px; }} }}
a {{ color: inherit; text-decoration: underline; text-underline-offset: 2px; }}
h1, h2, h3 {{ font-family: var(--display); font-weight: 600; letter-spacing: 0.01em; }}
.icon {{ width: 24px; height: 24px; color: var(--accent); display: block; flex: none; }}

/* Hero on a faint 32 px tile grid, the size of one map tile. */
header {{ position: relative; padding: 96px 0 104px; border-bottom: 1px solid var(--line); }}
header::before {{ content: ""; position: absolute; inset: 0; pointer-events: none;
  background-image: linear-gradient(var(--grid) 1px, transparent 1px),
                    linear-gradient(90deg, var(--grid) 1px, transparent 1px);
  background-size: 32px 32px; background-position: -1px -1px;
  -webkit-mask-image: linear-gradient(#000 40%, transparent);
          mask-image: linear-gradient(#000 40%, transparent); }}
.langs {{ position: absolute; top: 24px; left: 0; right: 0; text-align: right; font: 0.75rem var(--mono);
         color: var(--muted); z-index: 1; }}
.langs button {{ font: inherit; color: var(--muted); background: none; border: 0; padding: 4px; cursor: pointer; }}
.langs button:hover, .langs button[aria-pressed="true"] {{ color: var(--text); }}
.hero {{ position: relative; display: grid; gap: 48px 64px; align-items: center; }}
@media (min-width: 1000px) {{ .hero {{ grid-template-columns: 5fr 6fr; }} }}
.shot {{ display: block; width: 100%; height: auto; border: 1px solid var(--line);
        box-shadow: 0 24px 64px rgba(0, 0, 0, 0.5); }}
.slides {{ display: flex; overflow-x: auto; scroll-snap-type: x mandatory; scrollbar-width: none; outline: none; }}
.slides::-webkit-scrollbar {{ display: none; }}
.carousel {{ position: relative; min-width: 0; }}
.slide {{ flex: 0 0 100%; scroll-snap-align: start; margin: 0; padding: 1px; }}
.slide .shot {{ aspect-ratio: 3 / 2; object-fit: cover; object-position: left top; box-shadow: none; cursor: zoom-in; }}
/* While the carousel plays, a faint ring around the next arrow fills up; when it closes, the next slide shows. */
.controls .arrow {{ position: relative; }}
.ring {{ position: absolute; inset: 6px; width: 28px; height: 28px; transform: rotate(-90deg); pointer-events: none; }}
.ring circle {{ fill: none; stroke: var(--muted); stroke-width: 1; opacity: 0; stroke-dasharray: 84.82;
               stroke-dashoffset: 84.82; }}
.ring.run circle {{ opacity: 0.55; animation: ring 6s linear forwards; }}
.carousel:hover .ring.run circle {{ animation-play-state: paused; }}
@keyframes ring {{ to {{ stroke-dashoffset: 0; }} }}
.slide figcaption {{ margin-top: 12px; padding-right: 96px; font: 0.78rem/40px var(--mono); color: var(--muted);
                    white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }}
@media (max-width: 599px) {{ .slide figcaption {{ padding-right: 0; }}
  .carousel .controls {{ position: static; justify-content: center; margin-top: 4px; }} }}
.controls {{ position: absolute; right: 0; bottom: 0; display: flex; align-items: center; gap: 2px; }}
.controls button {{ display: flex; align-items: center; justify-content: center; width: 40px; height: 40px;
                   padding: 0; background: none; border: 0; cursor: pointer; color: var(--muted); }}
.controls button:hover {{ color: var(--text); }}
.controls button:focus-visible {{ outline: 1px solid var(--muted); }}

/* The large view: one screenshot, whole, over the dimmed page, with the same caption and arrows. */
.lightbox {{ width: 100vw; height: 100vh; max-width: none; max-height: none; margin: 0; padding: 32px; border: 0;
            background: transparent; color: var(--text); }}
.lightbox[open] {{ display: flex; flex-direction: column; align-items: center; justify-content: center; }}
.lightbox::backdrop {{ background: rgba(6, 7, 9, 0.88); backdrop-filter: blur(2px); }}
.lightbox img {{ display: block; max-width: min(1600px, 100%); max-height: calc(100vh - 140px); width: auto; height: auto;
                border: 1px solid var(--line); box-shadow: 0 24px 80px rgba(0, 0, 0, 0.6); cursor: zoom-out; }}
.lightbox .frame {{ display: flex; flex-direction: column; max-width: 100%; }}
.lightbox:focus {{ outline: none; }}
.lightbox .bar {{ display: flex; align-items: center; gap: 16px; margin-top: 12px; }}
.lightbox .bar p {{ flex: 1; margin: 0; font: 0.8rem/40px var(--mono); color: var(--muted); }}
.lightbox .controls {{ position: static; }}

h1 {{ font-size: clamp(2.6rem, 5vw, 3.8rem); line-height: 1; margin: 0 0 28px; white-space: nowrap;
      display: flex; align-items: center; gap: 0.3em; }}
h1 img {{ width: 1.05em; height: 1.05em; filter: drop-shadow(0 0 24px rgba(92, 224, 150, 0.25)); }}
.lede {{ font-size: clamp(1.2rem, 2.4vw, 1.5rem); line-height: 1.45; color: var(--text); max-width: 32em;
        margin: 0 0 16px; }}
.heritage {{ color: var(--muted); max-width: 40em; margin: 0 0 36px; }}
.actions {{ display: flex; flex-wrap: wrap; gap: 12px; }}
.btn {{ display: inline-block; padding: 12px 20px; text-decoration: none; font-weight: 600; font-size: 0.95rem;
       border: 1px solid var(--line); color: var(--text); background: var(--raise); border-radius: 2px; }}
.btn:hover {{ border-color: var(--muted); }}
.btn.primary {{ background: var(--accent); border-color: var(--accent); color: var(--on-accent); }}
.btn.primary:hover {{ filter: brightness(1.08); }}
.note {{ margin: 16px 0 0; font: 0.8rem var(--mono); color: var(--muted); letter-spacing: 0.02em; }}
.video {{ position: relative; aspect-ratio: 16 / 9; background: #000; border: 1px solid var(--line); }}
.video::after {{ content: ""; position: absolute; inset: -6px; border: 1px solid var(--grid); pointer-events: none; }}
.video iframe {{ position: absolute; inset: 0; width: 100%; height: 100%; border: 0; }}

section {{ padding: 72px 0; border-bottom: 1px solid var(--line); }}
h2 {{ font-size: clamp(1.8rem, 4vw, 2.4rem); line-height: 1.1; margin: 0 0 40px; }}
.sub {{ margin: -28px 0 40px; color: var(--muted); }}

/* One panel cut into four cells by hairlines (the grid's 1 px gaps show the line colour). */
.personas {{ display: grid; gap: 1px; background: var(--line); border: 1px solid var(--line);
            grid-template-columns: repeat(auto-fit, minmax(min(100%, 240px), 1fr)); }}
.persona {{ background: var(--bg); padding: 28px 28px 32px; display: flex; flex-direction: column;
           transition: background 0.2s; }}
.persona:hover {{ background: var(--raise); }}
.persona .tile {{ display: block; margin-bottom: 40px; }}
.persona .icon {{ width: 26px; height: 26px; }}
.persona h3 {{ font-size: 1.15rem; margin: 0 0 8px; }}
.persona p {{ margin: 0; color: var(--muted); font-size: 0.95rem; }}
.persona.soon .icon {{ color: var(--muted); }}
.persona .badge {{ margin-top: auto; padding-top: 16px; color: var(--muted); font: 500 0.7rem var(--mono); text-transform: uppercase; letter-spacing: 0.08em; }}

.bucket {{ display: grid; gap: 16px 48px; padding: 40px 0; border-top: 1px solid var(--line); }}
.bucket:last-child {{ padding-bottom: 0; }}
@media (min-width: 860px) {{ .bucket {{ grid-template-columns: 200px 1fr; }}
  .rail {{ position: sticky; top: 24px; align-self: start; }} }}
.rail h3 {{ font-size: 2rem; line-height: 1; margin: 0 0 10px; }}
.rail p {{ margin: 0; color: var(--muted); font-size: 0.9rem; max-width: 24em; }}
.items {{ list-style: none; margin: 0; padding: 0; display: grid; gap: 32px 40px;
         grid-template-columns: repeat(auto-fill, minmax(min(100%, 300px), 1fr)); }}
.item {{ border-top: 1px solid var(--line); padding-top: 14px; scroll-margin-top: 24px; }}
.item:target {{ border-top-color: var(--accent); }}
.rid {{ color: var(--muted); text-decoration: none; }}
.rid:hover {{ color: var(--text); text-decoration: underline; }}
.item h4 {{ font-size: 1rem; font-weight: 600; margin: 0 0 6px; }}
.item p {{ margin: 0; font-size: 0.9rem; color: var(--muted); }}
.item .who {{ margin-top: 10px; font: 0.72rem var(--mono); text-transform: uppercase; letter-spacing: 0.06em; }}
.empty {{ margin: 0; color: var(--muted); font: 0.85rem var(--mono); align-self: center; }}

.ways {{ display: grid; gap: 32px 40px; grid-template-columns: repeat(auto-fit, minmax(min(100%, 260px), 1fr)); }}
.way {{ border-top: 1px solid var(--line); padding-top: 14px; }}
.way h3 {{ font: 500 0.75rem var(--mono); text-transform: uppercase; letter-spacing: 0.1em; color: var(--accent);
          margin: 0 0 6px; display: flex; align-items: center; gap: 10px; }}
.way .icon {{ width: 20px; height: 20px; }}
.way p {{ margin: 0; color: var(--muted); }}

footer {{ padding: 32px 0 56px; font: 0.8rem var(--mono); color: var(--muted); }}
footer a {{ color: var(--muted); margin-right: 20px; }}
footer a:hover {{ color: var(--text); }}
</style>
</head>
<body>
<header>
  <nav class="wrap langs" aria-label="Language"><button data-lang="en" aria-pressed="true">EN</button> | <button data-lang="ko" aria-pressed="false">KR</button></nav>
  <div class="wrap hero">
    <div>
    <h1><img src="assets/app-256.png" alt="">StarDraft 2</h1>
    <p class="lede"><span data-i18n="lede">{L[lede]}</span></p>
    <p class="heritage"><span data-i18n="heritage">{L[heritage]}</span></p>
    <div class="actions">
      <a class="btn primary" href="{repo}/releases/latest"><span data-i18n="download">{L[download]}</span></a>
      <a class="btn" href="{discord}"><span data-i18n="discord">{L[discord]}</span></a>
    </div>
    <p class="note"><span data-i18n="builds">{L[builds]}</span></p>
    </div>
    <div class="carousel">
      <div class="slides" id="slides" tabindex="0" aria-label="{L[shots]}" data-i18n-aria-label="shots">
        <figure class="slide" aria-label="{L[of1]}" data-i18n-aria-label="of1">
          <img class="shot" src="assets/shots/map.webp" width="1890" height="1260"
            alt="{L[alt1]}" data-i18n-alt="alt1">
          <figcaption><span data-i18n="cap1">{L[cap1]}</span> &middot; <a href="https://scmscx.com/map/hksRyYbr">Crescent Dyne</a></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of2]}" data-i18n-aria-label="of2">
          <img class="shot" src="assets/shots/quick.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt2]}" data-i18n-alt="alt2">
          <figcaption><span data-i18n="cap2">{L[cap2]}</span> &middot; <a href="https://scmscx.com/map/3pV5r4BV">Cave Wars</a></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of3]}" data-i18n-aria-label="of3">
          <img class="shot" src="assets/shots/generate.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt3]}" data-i18n-alt="alt3">
          <figcaption><span data-i18n="cap3">{L[cap3]}</span></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of4]}" data-i18n-aria-label="of4">
          <img class="shot" src="assets/shots/triggers.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt4]}" data-i18n-alt="alt4">
          <figcaption><span data-i18n="cap4">{L[cap4]}</span></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of5]}" data-i18n-aria-label="of5">
          <img class="shot" src="assets/shots/data.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt5]}" data-i18n-alt="alt5">
          <figcaption><span data-i18n="cap5">{L[cap5]}</span></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of6]}" data-i18n-aria-label="of6">
          <img class="shot" src="assets/shots/weapons.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt6]}" data-i18n-alt="alt6">
          <figcaption><span data-i18n="cap6">{L[cap6]}</span></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of7]}" data-i18n-aria-label="of7">
          <img class="shot" src="assets/shots/buttons.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt7]}" data-i18n-alt="alt7">
          <figcaption><span data-i18n="cap7">{L[cap7]}</span></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of8]}" data-i18n-aria-label="of8">
          <img class="shot" src="assets/shots/images.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt8]}" data-i18n-alt="alt8">
          <figcaption><span data-i18n="cap8">{L[cap8]}</span></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of9]}" data-i18n-aria-label="of9">
          <img class="shot" src="assets/shots/briefing.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt9]}" data-i18n-alt="alt9">
          <figcaption><span data-i18n="cap9">{L[cap9]}</span> &middot; <a href="https://staredit.net/topic/18491/">Underdogs</a></figcaption>
        </figure>
        <figure class="slide" aria-label="{L[of10]}" data-i18n-aria-label="of10">
          <img class="shot" src="assets/shots/korean.webp" width="1890" height="1260" loading="lazy"
            alt="{L[alt10]}" data-i18n-alt="alt10">
          <figcaption><span data-i18n="cap10">{L[cap10]}</span></figcaption>
        </figure>
      </div>
      <div class="controls">
        <button class="arrow" data-step="-1" aria-label="{L[prev]}" data-i18n-aria-label="prev"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M10 3L5 8L10 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
        <button class="arrow" data-step="1" aria-label="{L[next]}" data-i18n-aria-label="next"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M6 3L11 8L6 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg><svg class="ring" viewBox="0 0 28 28" aria-hidden="true"><circle cx="14" cy="14" r="13.5"/></svg></button>
      </div>
    </div>
  </div>
</header>

<section>
  <div class="wrap">
    <h2><span data-i18n="who">{L[who]}</span></h2>
    <div class="personas">
{personas}
    </div>
  </div>
</section>

<section id="video">
  <div class="wrap">
    <div class="video">
      <iframe src="https://www.youtube-nocookie.com/embed/{video}" title="{L[video]}" data-i18n-title="video" loading="lazy"
        referrerpolicy="strict-origin-when-cross-origin"
        allow="accelerometer; encrypted-media; gyroscope; picture-in-picture; fullscreen" allowfullscreen></iframe>
    </div>
  </div>
</section>

<section id="roadmap">
  <div class="wrap">
    <h2><span data-i18n="roadmap">{L[roadmap]}</span></h2>
    <p class="sub"><span data-i18n="sourced">{L[sourced]}</span></p>
    <div class="buckets">
{buckets}
    </div>
  </div>
</section>

<section id="feedback">
  <div class="wrap">
    <h2><span data-i18n="feedback">{L[feedback]}</span></h2>
    <div class="ways">
      <div class="way"><h3>{i_select}<span data-i18n="way1">{L[way1]}</span></h3><p><span data-i18n="way1text">{L[way1text]}</span></p></div>
      <div class="way"><h3>{i_feedback}<span data-i18n="way2">{L[way2]}</span></h3><p><span data-i18n="way2text">{L[way2text]}</span></p></div>
      <div class="way"><h3>{i_issue}<span data-i18n="way3">{L[way3]}</span></h3><p><span data-i18n="way3text">{L[way3text]}</span></p></div>
    </div>
  </div>
</section>

<footer>
  <div class="wrap">
    <a href="{repo}">GitHub</a><a href="{repo}/blob/main/docs/user/CHANGELOG.md"><span data-i18n="changelog">{L[changelog]}</span></a><a href="{discord}">Discord</a><a href="{repo}/issues"><span data-i18n="report">{L[report]}</span></a>
  </div>
</footer>
<dialog class="lightbox" id="lightbox" aria-label="{L[shot]}" data-i18n-aria-label="shot" tabindex="-1">
  <div class="frame">
  <img id="lb-img" src="" alt="">
  <div class="bar">
    <p id="lb-cap"></p>
    <div class="controls">
      <button class="arrow" data-step="-1" aria-label="{L[prev]}" data-i18n-aria-label="prev"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M10 3L5 8L10 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
      <button class="arrow" data-step="1" aria-label="{L[next]}" data-i18n-aria-label="next"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M6 3L11 8L6 13" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button>
      <button class="close" aria-label="{L[close]}" data-i18n-aria-label="close"><svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true"><path d="M4 4L12 12M12 4L4 12" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg></button>
    </div>
  </div>
  </div>
</dialog>
<script>
// The language: the viewer's choice (EN | KR, kept in this browser), else ?lang=ko or en in the address, else the
// browser's languages (Korean first: Korean). The page is written in English; KO has the Korean of each data-i18n key.
(() => {{
  const KO = {ko_json};
  const ATTRS = ["alt", "aria-label", "title", "content"];
  const texts = [...document.querySelectorAll("[data-i18n]")].map(e => [e, e.dataset.i18n, e.innerHTML]);
  const attrs = ATTRS.flatMap(a => [...document.querySelectorAll(`[data-i18n-${{a}}]`)]
    .map(e => [e, a, e.getAttribute(`data-i18n-${{a}}`), e.getAttribute(a)]));
  const set = lang => {{
    const ko = lang === "ko";
    document.documentElement.lang = lang;
    for (const [e, k, en] of texts) e.innerHTML = ko && k in KO ? KO[k] : en;
    for (const [e, a, k, en] of attrs) e.setAttribute(a, ko && k in KO ? KO[k] : en);
    document.querySelectorAll(".langs button").forEach(b => b.setAttribute("aria-pressed", b.dataset.lang === lang));
  }};
  let lang = null;
  try {{ lang = localStorage.getItem("lang"); }} catch (e) {{}}
  const asked = new URLSearchParams(location.search).get("lang");
  if (asked === "ko" || asked === "en") lang = asked;
  if (lang !== "ko" && lang !== "en")
    lang = (navigator.languages || [navigator.language]).find(l => /^(ko|en)(-|$)/i.test(l || ""))?.slice(0, 2).toLowerCase() || "en";
  set(lang);
  document.querySelectorAll(".langs button").forEach(b => b.addEventListener("click", () => {{
    set(b.dataset.lang);
    try {{ localStorage.setItem("lang", b.dataset.lang); }} catch (e) {{}}
  }}));
}})();

// The screenshots carousel: the arrows (and the arrow keys) scroll the slides, from the last to the first again.
// It plays by itself until the viewer uses it (an arrow, a key, a swipe or a scroll): a faint ring around the next
// arrow fills up, and when it closes the next slide shows. The ring stops while the mouse is over the carousel. With
// reduced motion, the slides change at once instead of sliding.
(() => {{
  const slides = document.getElementById("slides"), n = slides.children.length;
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const at = () => Math.round(slides.scrollLeft / slides.clientWidth);
  const go = i => slides.scrollTo({{ left: ((i + n) % n) * slides.clientWidth, behavior: still ? "instant" : "smooth" }});
  const ring = document.querySelector(".carousel .ring");
  let playing = true;
  const run = () => {{
    ring.classList.remove("run");
    void ring.getBoundingClientRect();  // starts the animation again
    if (playing) ring.classList.add("run");
  }};
  ring.addEventListener("animationend", () => {{
    if (!playing) return;
    go(at() + 1);
    run();
  }});
  const stop = () => {{ playing = false; ring.classList.remove("run"); }};
  run();
  document.querySelectorAll(".carousel .controls .arrow").forEach(a => a.addEventListener("click", () => {{
    stop();
    go(at() + +a.dataset.step);
  }}));
  slides.addEventListener("keydown", e => {{
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") stop(), e.preventDefault(), go(at() + (e.key === "ArrowLeft" ? -1 : 1));
  }});
  for (const ev of ["pointerdown", "wheel", "touchstart"]) slides.addEventListener(ev, stop, {{ passive: true }});

  // A click on a screenshot opens the large view; its arrows and keys go through the same slides. Esc, the close
  // button, a click beside the picture or on it closes it, at the slide it showed last.
  const box = document.getElementById("lightbox"), img = document.getElementById("lb-img");
  const cap = document.getElementById("lb-cap");
  let cur = 0;
  const show = i => {{
    cur = (i + n) % n;
    const s = slides.children[cur], pic = s.querySelector("img");
    img.src = pic.src, img.alt = pic.alt, cap.innerHTML = s.querySelector("figcaption").innerHTML;
  }};
  [...slides.children].forEach((s, i) => s.querySelector("img").addEventListener("click", () => {{
    stop();
    show(i);
    box.showModal();
    box.focus();
  }}));
  box.querySelectorAll(".arrow").forEach(a => a.addEventListener("click", () => show(cur + +a.dataset.step)));
  box.querySelector(".close").addEventListener("click", () => box.close());
  box.addEventListener("click", e => {{ if (e.target === box || e.target === img) box.close(); }});
  box.addEventListener("keydown", e => {{
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") e.preventDefault(), show(cur + (e.key === "ArrowLeft" ? -1 : 1));
  }});
  box.addEventListener("close", () => slides.scrollTo({{ left: cur * slides.clientWidth, behavior: "instant" }}));
}})();
</script>
</body>
</html>
"""

if __name__ == "__main__":
    items, intros = parse(ROADMAP.read_text(encoding="utf-8"))
    missing = [i["id"] for i in items if i["id"] not in KO_ROADMAP]
    if missing:  # in Korean, these show in English until KO_ROADMAP has them
        print("No Korean text for " + ", ".join(missing) + " (KO_ROADMAP in tools/build_site.py)", file=sys.stderr)
    page = build(items, intros)
    assets = {OUT.parent / "assets" / f"app-{n}.png": ICONS / "app" / "png" / f"app-{n}.png" for n in (32, 256)}
    if "--check" in sys.argv:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != page or any(not d.exists() or d.read_bytes() != s.read_bytes() for d, s in assets.items()):
            sys.exit("docs/index.html is out of date: run python tools/build_site.py")
    else:
        for dst, src in assets.items():
            dst.parent.mkdir(exist_ok=True)
            shutil.copyfile(src, dst)
        OUT.write_text(page, encoding="utf-8", newline="\n")
