"""Releases of StarDraft 2: the unreleased work, the checks, the screenshots and the release itself.

A push to master never makes a release. A tag v<version> does (.github/workflows/release.yml, with the files that
build.yml built and tested for the commit pushed with it). This script makes that tag
the same way each time, after the owner approves the scope and the release notes. Run it with `python -I`:

    pending                     The newest release tag, the commits after it (* = not pushed) and the next version.
    shots <version> [--measure] The screenshots of tools/shots/<version>.json: snapshots of the testing build with
                                no window (tools/sdl_snapshots.py), cropped into docs/user/images/<version>/.
                                --measure: only the captures.
    check <version>             All checks, without a change: the version, the push account, the working tree, the
                                changelog section and its pictures, the UI text, the translation tables, the build.
    publish <version> --yes     check, then commit the changelog and its pictures ("Changelog <version>"), tag
                                v<version>, push master and the tag together, wait for CI and confirm the release.
                                --dry-run: show the commands and change nothing.
    untag <version>             Delete the tag v<version> here and on GitHub, when its run stopped before the release
                                was made (then publish again). Refused when the release is there.

CI uses these (they need only Python and git):

    notes <version> [--tag <tag>] [--out <file>] [--images]
                                The release text: the version's changelog section, then the base section (the last
                                one: what StarDraft is). With --tag, picture paths become addresses at that tag.
                                --images: the version's picture files, one on each line (the Discord post).
    discord <version> --title <t> --url <u> [--channel all|updates|general] [--screenshots-only]
                                The Discord post (CI): in each channel with a webhook (DISCORD_RELEASE_WEBHOOK,
                                DISCORD_GENERAL_WEBHOOK), "**<title>** is out: <url>" (in #updates after the @New Release ping) with
                                the section's video links, then the screenshots in one message (each with its text),
                                then the version's section as Discord text: in a thread on the first message when it is
                                more than one message long (the bot starts it: DISCORD_BOT_TOKEN; without it, in the
                                channel), else in the channel. --screenshots-only: the screenshots alone.
    discord-text <version>      The section as the Discord messages have it (to check it).
    title <version>             "StarDraft 2 - v<version> (build <commit>)", the commit of HEAD (7 characters, as
                                cmake/build_info.cmake writes it: Check for updates compares the two).
    ci-check <tag>              Before the tag's release: the tag is a version and the newest one, its section and
                                pictures are good, and the commit is on master.
"""

import calendar
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = ROOT / "docs" / "user" / "CHANGELOG.md"
IMAGES = ROOT / "docs" / "user" / "images"
SHOTS = ROOT / "tools" / "shots"
REPO = "stardraft-editor/stardraft"
PUSH_ACCOUNT = "milestone-dev"
MAX_IMAGES = 10  # a Discord post carries at most 10 files
GROUPS = {"New", "Map", "Data", "Triggers", "Briefings", "Assets", "The program", "CMBW mod", "Fixes", "Known problems"}
# Words that tell of the work, not of what a user gets (a warning: the owner approves the text).
DEV_WORDS = re.compile(r"\b(CI|workflow|refactor\w*|lint|datashots?|self-test|commit|cmake|pdb|GitHub Actions|"
                       r"unit tests?|test suite)\b", re.I)
ACTOR = re.compile(r"^(The program|StarDraft( 2)?|It) (asks|knows|wants|tries|decides|remembers|shows its)\b")
VERSION_RE = re.compile(r"^\d+(\.\d+){1,3}$")


class Fail(Exception):
    pass


def run(cmd, check=True, env=None, capture=True, cwd=ROOT):
    r = subprocess.run(cmd, cwd=cwd, env=env, text=True, encoding="utf-8", errors="replace",
                       stdout=subprocess.PIPE if capture else None, stderr=subprocess.PIPE if capture else None)
    if check and r.returncode:
        raise Fail(f"{' '.join(map(str, cmd))} stopped ({r.returncode}): {(r.stderr or r.stdout or '').strip()[:600]}")
    return (r.stdout or "").rstrip() if capture else ""  # (not lstrip: git status lines start with a space)


def git(*args, check=True):
    return run(["git", *args], check=check)


def vkey(v):
    return tuple(int(x) for x in v.split("."))


# --- the changelog ---------------------------------------------------------------------------------------------------

def sections():
    """The changelog's "## " sections in their order: [(heading, [lines])]."""
    out = []
    for line in CHANGELOG.read_text(encoding="utf-8").splitlines():
        if line.startswith("## "):
            out.append((line[3:].strip(), []))
        elif out:
            out[-1][1].append(line)
    return out


def section_of(version, secs=None):
    for name, lines in secs or sections():
        if name == version or name.startswith(version + " "):
            return name, lines
    return None, None


def image_refs(lines):
    seen = []
    for m in re.finditer(r"\]\((images/[^)\s]+)\)", "\n".join(lines)):
        if m.group(1) not in seen:
            seen.append(m.group(1))
    return seen


def notes_text(version, tag=None):
    secs = sections()
    name, lines = section_of(version, secs)
    if not name:
        raise Fail(f"docs/user/CHANGELOG.md has no section \"## {version}\"")
    base, base_lines = secs[-1]
    out = []
    if name != base:
        out += [f"## What is new in {version}", *lines, ""]
    out += [f"## About StarDraft ({base})", *base_lines]
    text = "\n".join(out)
    if tag:
        raw = f"https://raw.githubusercontent.com/{os.environ.get('GITHUB_REPOSITORY', REPO)}/{tag}/docs/user/"
        text = re.sub(r"\]\((images/[^)\s]+)\)", lambda m: f"]({raw}{m.group(1)})", text)
    return text


def check_section(version, problems, warnings):
    """The version's section: there, with bullets, in the groups the file uses, each line a short fact."""
    secs = sections()
    name, lines = section_of(version, secs)
    if not name:
        problems.append(f"docs/user/CHANGELOG.md has no section \"## {version}\"")
        return []
    bullets = [l for l in lines if l.startswith("- ")]
    if not bullets:
        problems.append(f"the section \"## {name}\" has no lines (\"- ...\")")
    for l in lines:
        if l.startswith("### ") and l[4:].strip() not in GROUPS:
            warnings.append(f"group \"{l[4:].strip()}\" is not one of: {', '.join(sorted(GROUPS))}")
    entries, cur = [], None  # a bullet and its continuation lines
    for l in lines:
        if l.startswith("- "):
            cur = l[2:].strip()
            entries.append(cur)
        elif cur is not None and l.startswith("  ") and l.strip():
            entries[-1] = entries[-1] + " " + l.strip()
        else:
            cur = None
    for e in entries:
        if e[:1].islower():
            warnings.append(f"starts with a small letter: \"{e[:70]}\"")
        if not e.rstrip().endswith((".", ".)", "\"", "”", ")")):
            warnings.append(f"no full stop at the end: \"{e[-70:]}\"")
        if DEV_WORDS.search(e):
            warnings.append(f"a word about the work, not the feature (\"{DEV_WORDS.search(e).group(0)}\"): \"{e[:70]}\"")
        if ACTOR.search(e):
            warnings.append(f"the program as an actor: \"{e[:70]}\"")
    refs = image_refs(lines)
    if len(refs) > MAX_IMAGES:
        problems.append(f"{len(refs)} pictures in the section; a Discord post carries {MAX_IMAGES}")
    for r in refs:
        if not (CHANGELOG.parent / r).is_file():
            problems.append(f"the picture {r} is not in docs/user/")
        elif not r.startswith(f"images/{version}/"):
            if git("ls-files", "--", f"docs/user/{r}", check=False):
                warnings.append(f"the picture {r} is not in images/{version}/")
            else:  # publish commits only images/<version>/: CI would not find it
                problems.append(f"the picture {r} is not committed and not in images/{version}/")
        elif not re.fullmatch(r"[a-z0-9-]+\.(png|mp4)", Path(r).name):
            warnings.append(f"picture name {Path(r).name}: use small letters, digits and - (.png)")
    folder = IMAGES / version
    if folder.is_dir():
        used = {Path(r).name for r in refs}
        for f in sorted(folder.iterdir()):
            if f.is_file() and f.name not in used:
                warnings.append(f"docs/user/images/{version}/{f.name} is not in the section")
    if secs and secs[0][0] != name:
        problems.append(f"\"## {name}\" is not the first section (the newest version goes first)")
    return refs


# --- tags and commits ------------------------------------------------------------------------------------------------

def release_tags():
    tags = [t[1:] for t in git("tag", "-l", "v*").split() if VERSION_RE.match(t[1:])]
    return sorted(tags, key=vkey)


def next_version(latest):
    p = latest.split(".")
    p[-1] = str(int(p[-1]) + 1)
    return ".".join(p)


def unreleased():
    """[(sha, subject, pushed)] after the newest release tag, oldest first."""
    tags = release_tags()
    since = f"v{tags[-1]}..HEAD" if tags else "HEAD"
    pushed = set(git("rev-list", "origin/master", check=False).split())
    out = []
    for line in git("log", "--reverse", "--format=%H %s", since).splitlines():
        sha, _, subject = line.partition(" ")
        out.append((sha, subject, sha in pushed))
    return out


def push_token():
    t = run(["gh", "auth", "token", "-u", PUSH_ACCOUNT], check=False)
    if not t:
        raise Fail(f"gh has no login for {PUSH_ACCOUNT}: gh auth login (as {PUSH_ACCOUNT})")
    return t


def gh_env(token):
    return {**os.environ, "GH_TOKEN": token}


def push_env(token):
    return {**os.environ, "RELEASE_PUSH_TOKEN": token, "GH_TOKEN": token}


def push_git_args(token_env_name="RELEASE_PUSH_TOKEN"):
    """git -c options that push as PUSH_ACCOUNT without changing gh's active account (other sessions use another)."""
    helper = f"!f() {{ echo username=x-access-token; echo \"password=${token_env_name}\"; }}; f"
    return ["-c", "credential.helper=", "-c", f"credential.helper={helper}"]


# --- commands --------------------------------------------------------------------------------------------------------

def cmd_pending(args):
    tags = release_tags()
    latest = tags[-1] if tags else None
    git("fetch", "--quiet", "origin", "master", check=False)
    commits = unreleased()
    print(f"Newest release: v{latest}" if latest else "No release tag yet")
    print(f"Next version (a suggestion; the owner names it): {next_version(latest) if latest else '0.5.0.1'}")
    print(f"{len(commits)} commits after it (* = not pushed):")
    for sha, subject, pushed in commits:
        print(f"  {'' if pushed else '*'}{sha[:7]} {subject}")


def cmd_notes(args):
    version = args[0]
    tag = opt(args, "--tag")
    out = opt(args, "--out")
    if "--images" in args:
        _, lines = section_of(version)
        if lines is None:
            raise Fail(f"docs/user/CHANGELOG.md has no section \"## {version}\"")
        for r in image_refs(lines)[:MAX_IMAGES]:
            if (CHANGELOG.parent / r).is_file():
                print(f"docs/user/{r}")
        return
    text = notes_text(version, tag)
    if out:
        Path(out).write_text(text + "\n", encoding="utf-8")
    else:
        sys.stdout.buffer.write((text + "\n").encode("utf-8"))


def image_alts(lines):
    """The pictures of a section with their text: [(path, alt)], in their order, each one once. A video is a link
    to an .mp4 ("[Video: what it shows](images/<version>/name.mp4)"): it goes to Discord as a picture does."""
    seen, out = set(), []
    for m in re.finditer(r"(!?)\[([^\]]*)\]\((images/[^)\s]+)\)", "\n".join(lines)):
        if (m.group(1) or m.group(3).endswith(".mp4")) and m.group(3) not in seen:
            seen.add(m.group(3))
            out.append((m.group(3), m.group(2)))
    return out


DISCORD_LIMIT = 1900  # a message holds 2000 characters at most


def discord_text(lines):
    """The section as Discord messages: its headings and bullets as Discord markdown, each bullet on one line (the
    changelog wraps them), no pictures (they go as messages of their own); split at lines into messages that fit."""
    out = []
    for raw in lines:
        line = raw.rstrip()
        if line.lstrip().startswith("![") or line.lstrip().startswith("[Video"):
            continue
        if line.startswith("### "):
            out += ["", "**" + line[4:].strip() + "**"]
        elif line.startswith("- "):
            out.append(line)
        elif line.startswith("  ") and line.strip() and out and out[-1].startswith("- "):
            out[-1] += " " + line.strip()  # (a wrapped bullet's next line)
        elif line.strip():
            out.append(line.strip())
    # Groups (a heading and its bullets) whole in a message where they fit; a group longer than a message by its lines.
    groups = []
    for line in out:
        if line.startswith("**") or not groups:
            groups.append([])
        if line:
            groups[-1].append(line[:DISCORD_LIMIT - 1] + "\u2026" if len(line) > DISCORD_LIMIT else line)
    messages, cur = [], ""
    def put(text):
        nonlocal cur
        if cur and len(cur) + 2 + len(text) > DISCORD_LIMIT:
            messages.append(cur)
            cur = ""
        cur = (cur + "\n\n" + text) if cur else text
    for g in groups:
        text = "\n".join(g)
        if len(text) <= DISCORD_LIMIT:
            put(text)
            continue
        part = ""
        for line in g:
            if part and len(part) + 1 + len(line) > DISCORD_LIMIT:
                put(part)
                part = ""
            part = (part + "\n" + line) if part else line
        if part:
            put(part)
    if cur:
        messages.append(cur)
    return messages


def cmd_discord_text(args):
    _, lines = section_of(args[0])
    if lines is None:
        raise Fail(f"docs/user/CHANGELOG.md has no section \"## {args[0]}\"")
    for i, m in enumerate(discord_text(lines)):
        sys.stdout.buffer.write((f"--- message {i + 1} ({len(m)} characters)\n{m}\n").encode("utf-8"))


# The Discord role that a new release pings ("@New Release": people take it to hear of releases).
DISCORD_RELEASE_ROLE = "1562910841257332786"


def discord_send(hook, content, file=None, ping_role=None, files=None, thread_id=None):
    """One webhook message: its text, and picture files as its attachments (`file`, or `files`: [(path, text)], at most
    10, each with its text as the attachment's description). `thread_id`: into that thread. Waits when Discord says to
    (429). ping_role: a role id that the message may ping (the text has its mention); no other mention pings. Returns
    the message (its id and channel_id)."""
    import urllib.error
    import urllib.request
    files = list(files or ([(file, "")] if file else []))[:10]
    boundary = "----stardraft" + str(int(time.time() * 1000))
    body = b""
    def part(headers, data):
        return (f"--{boundary}\r\n" + "".join(f"{h}\r\n" for h in headers) + "\r\n").encode() + data + b"\r\n"
    payload = {"content": content, "allowed_mentions": {"roles": [ping_role] if ping_role else []}}
    if files:
        payload["attachments"] = [{"id": i, "filename": Path(f).name, "description": (t or "")[:1000]} for i, (f, t) in enumerate(files)]
    body += part(['Content-Disposition: form-data; name="payload_json"', "Content-Type: application/json"],
                 json.dumps(payload).encode("utf-8"))
    for i, (f, _) in enumerate(files):
        kind = "video/mp4" if Path(f).suffix == ".mp4" else "image/png"
        body += part([f'Content-Disposition: form-data; name="files[{i}]"; filename="{Path(f).name}"', f"Content-Type: {kind}"],
                     Path(f).read_bytes())
    body += f"--{boundary}--\r\n".encode()
    address = hook + ("&" if "?" in hook else "?") + "wait=true" + (f"&thread_id={thread_id}" if thread_id else "")
    for attempt in range(5):
        req = urllib.request.Request(address, data=body, method="POST",
                                     headers={"Content-Type": f"multipart/form-data; boundary={boundary}", "User-Agent": "StarDraft release"})
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                try:
                    return json.loads(resp.read() or b"{}")
                except ValueError:
                    return {}
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt < 4:
                try:
                    wait = float(json.loads(e.read() or b"{}").get("retry_after", 2))
                except ValueError:
                    wait = 2
                time.sleep(min(30, wait + 0.5))
                continue
            raise Fail(f"Discord: {e.code} {e.read()[:300]!r}")
    return {}


def discord_thread(message, name):
    """A thread on a message that a webhook posted (the bot starts it: DISCORD_BOT_TOKEN); its id, or None when there
    is no token or it did not go."""
    import urllib.error
    import urllib.request
    token = os.environ.get("DISCORD_BOT_TOKEN")
    if not token or not message.get("id") or not message.get("channel_id"):
        return None
    req = urllib.request.Request(f"https://discord.com/api/v10/channels/{message['channel_id']}/messages/{message['id']}/threads",
                                 data=json.dumps({"name": name[:100], "auto_archive_duration": 10080}).encode(), method="POST",
                                 headers={"Authorization": f"Bot {token}", "Content-Type": "application/json",
                                          "User-Agent": "StarDraft release"})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.loads(resp.read() or b"{}").get("id")
    except (urllib.error.HTTPError, urllib.error.URLError, ValueError) as e:
        print(f"warning: no thread for the notes ({e}); they go in the channel")
        return None


def discord_links(lines):
    """The section's lines with an address (a video): they go in the release's first message, where Discord shows
    them (a link in a thread shows there only)."""
    out = []
    for m in discord_text(lines):
        for l in m.splitlines():
            if "https://" in l and "images/" not in l:
                out.append(l)
    return out


def cmd_discord(args):
    version = args[0]
    title, url = opt(args, "--title"), opt(args, "--url")
    channel = opt(args, "--channel") or "all"
    only_shots = "--screenshots-only" in args
    if not only_shots and not (title and url):
        raise Fail("discord needs --title and --url (or --screenshots-only)")
    _, lines = section_of(version)
    if lines is None:
        raise Fail(f"docs/user/CHANGELOG.md has no section \"## {version}\"")
    shots = [(CHANGELOG.parent / p, alt) for p, alt in image_alts(lines)[:MAX_IMAGES] if (CHANGELOG.parent / p).is_file()]
    hooks = []
    if channel in ("all", "updates") and os.environ.get("DISCORD_RELEASE_WEBHOOK"):
        hooks.append(("updates", os.environ["DISCORD_RELEASE_WEBHOOK"]))
    if channel in ("all", "general") and os.environ.get("DISCORD_GENERAL_WEBHOOK"):
        hooks.append(("general", os.environ["DISCORD_GENERAL_WEBHOOK"]))
    if not hooks:
        raise Fail(f"no Discord webhook for {channel} (DISCORD_RELEASE_WEBHOOK, DISCORD_GENERAL_WEBHOOK)")
    failed = []
    for name, hook in hooks:
        try:
            texts = discord_text(lines)
            long = len(texts) > 1  # (more than one message of text: the notes go in a thread)
            first = {}
            if not only_shots:
                links = discord_links(lines)
                head = f"**{title}** is out: {url}" + ("".join("\n" + l for l in links) if links else "")
                if long and os.environ.get("DISCORD_BOT_TOKEN"):
                    head += "\nThe full notes are in the thread."
                if name == "updates":  # (one ping for a release: in #updates)
                    first = discord_send(hook, f"<@&{DISCORD_RELEASE_ROLE}> " + head, ping_role=DISCORD_RELEASE_ROLE)
                else:
                    first = discord_send(hook, head)
            if shots:  # (the pictures in the channel, all in one message, each with its text)
                time.sleep(1)
                discord_send(hook, "", files=shots)
            where = "in the channel"
            if not only_shots:
                thread = discord_thread(first, f"What is new in {version}") if long else None
                for text in texts:  # (the changelog, as Discord shows text)
                    time.sleep(1)
                    discord_send(hook, text, thread_id=thread)
                where = "in a thread" if thread else "in the channel"
            print(f"posted in {name}: {'' if only_shots else f'the release, the changelog ({where}) and '}{len(shots)} screenshots in one message")
        except Fail as e:
            failed.append(f"{name}: {e}")
    if failed:
        raise Fail("Discord posts that did not go (post again with Actions > Post screenshots to Discord):\n  " + "\n  ".join(failed))


def cmd_title(args):
    print(f"StarDraft 2 - v{args[0]} (build {git('rev-parse', 'HEAD')[:7]})")


def cmd_ci_check(args):
    tag = args[0]
    problems, warnings = [], []
    if not (tag.startswith("v") and VERSION_RE.match(tag[1:])):
        raise Fail(f"the tag {tag} is not v<version> (v0.5.0.8): no release")
    version = tag[1:]
    check_section(version, problems, warnings)
    # Only the newest version is released: a run of an older tag again must not make it Latest (Check for updates
    # would offer it to everyone). A post that did not go: Actions > Post screenshots to Discord.
    remote = [l.split("refs/tags/v", 1)[1] for l in run(["git", "ls-remote", "--tags", "origin", "v*"], check=False).splitlines()
              if "refs/tags/v" in l and not l.endswith("^{}")]
    newer = [v for v in remote if VERSION_RE.match(v) and vkey(v) > vkey(version)]
    if newer:
        problems.append(f"v{max(newer, key=vkey)} is newer: an older tag makes no release")
    sha = os.environ.get("GITHUB_SHA") or git("rev-parse", "HEAD")
    if os.environ.get("GH_TOKEN"):  # the tag's commit is on master (master has it, or is it)
        status = run(["gh", "api", f"repos/{os.environ.get('GITHUB_REPOSITORY', REPO)}/compare/master...{sha}",
                      "--jq", ".status"], check=False)
        if status not in ("behind", "identical"):
            problems.append(f"the tagged commit {sha[:7]} is not on master (compare: {status or 'no answer'})")
    for w in warnings:
        print(f"warning: {w}")
    if problems:
        raise Fail("\n".join(problems))
    print(f"{tag}: ready to release")


def cmd_check(args, quiet_build=False):
    version = args[0]
    problems, warnings = [], []
    if not VERSION_RE.match(version):
        raise Fail(f"{version} is not a version (0.5.0.8)")
    tags = release_tags()
    if tags and vkey(version) <= vkey(tags[-1]):
        problems.append(f"{version} is not after the newest release v{tags[-1]}")
    if git("tag", "-l", f"v{version}"):
        problems.append(f"the tag v{version} is here already")
    if run(["git", "ls-remote", "--tags", "origin", f"v{version}"], check=False):
        problems.append(f"the tag v{version} is on GitHub already")
    # The push account
    try:
        login = run(["gh", "api", "user", "--jq", ".login"], env=gh_env(push_token()), check=False)
        if login != PUSH_ACCOUNT:
            problems.append(f"the {PUSH_ACCOUNT} token answers as \"{login or 'nothing'}\"")
    except Fail as e:
        problems.append(str(e))
    # The working tree: only the changelog and this version's pictures may be open (publish commits them)
    if git("rev-parse", "--abbrev-ref", "HEAD") != "master":
        problems.append("not on master")
    allowed = ("docs/user/CHANGELOG.md", f"docs/user/images/{version}/", f"tools/shots/{version}.json")
    for line in git("status", "--porcelain").splitlines():
        path = line[3:].strip().strip('"')
        if not path.startswith(allowed) and not line.startswith("??"):
            problems.append(f"a change that is not committed: {path}")
    git("fetch", "--quiet", "origin", "master", check=False)
    if git("rev-list", "--count", "HEAD..origin/master", check=False) not in ("", "0"):
        problems.append("GitHub's master has commits that are not here: git pull --ff-only")
    # The section and its pictures
    check_section(version, problems, warnings)
    # The UI text and the translation tables, as CI checks them
    lint = subprocess.run([sys.executable, "-I", "tools/ui_text_lint.py"], cwd=ROOT, capture_output=True, text=True)
    if lint.returncode:
        problems.append("UI text: " + (lint.stdout + lint.stderr).strip().splitlines()[-1])
    gen = subprocess.run([sys.executable, "-I", "tools/ui_lang_gen.py"], cwd=ROOT, capture_output=True, text=True)
    if gen.returncode or git("status", "--porcelain", "--", "apps/editor/res/lang"):
        problems.append("the translation tables are not made from the .tsv files: python -I tools/ui_lang_gen.py, commit")
    # The build: all targets, as CI builds them (in the testing build folder: the user's StarDraft 2.exe stays alone)
    if "--no-build" not in args:
        build = ROOT / (opt(args, "--build-dir") or "build")
        if not (build / "CMakeCache.txt").is_file():
            problems.append(f"no build folder {build.name}: cmake -S . -B {build.name} -DSTARDRAFT_EXE_NAME=stardraft-testing")
        else:
            t = time.time()
            r = subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel"], cwd=ROOT,
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
            if r.returncode:
                errs = [l for l in r.stdout.splitlines() if ": error" in l or "error C" in l][:5]
                problems.append("the build stops:\n    " + "\n    ".join(errs or r.stdout.splitlines()[-5:]))
            elif not quiet_build:
                print(f"build: all targets, {time.time() - t:.0f} s")
    # The report
    commits = unreleased()
    print(f"v{version} after v{tags[-1] if tags else '-'}: {len(commits)} commits (* = not pushed)")
    for sha, subject, pushed in commits:
        print(f"  {'' if pushed else '*'}{sha[:7]} {subject[:110]}")
    for w in warnings:
        print(f"warning: {w}")
    if problems:
        raise Fail("not ready:\n  " + "\n  ".join(problems))
    print(f"v{version}: ready (publish {version} --yes)")


def cmd_publish(args):
    version = args[0]
    dry = "--dry-run" in args
    if not dry and "--yes" not in args:
        raise Fail("publish makes a public release: add --yes (or --dry-run to see the steps)")
    cmd_check(args)
    token = push_token()
    env = push_env(token)
    tag = f"v{version}"
    open_paths = [l[3:].strip().strip('"') for l in git("status", "--porcelain", "--", "docs/user/CHANGELOG.md",
                                                         f"docs/user/images/{version}", f"tools/shots/{version}.json").splitlines()]
    steps = []
    if open_paths:
        steps.append(["git", "add", "--", *open_paths])
        steps.append(["git", "commit", "-q", "-m", f"Changelog {version}"])
    steps.append(["git", "tag", tag])
    steps.append(["git", *push_git_args(), "push", "--atomic", "origin", "master", tag])
    # master can move during the build: look again just before the tag
    git("fetch", "--quiet", "origin", "master", check=False)
    if git("rev-list", "--count", "HEAD..origin/master", check=False) not in ("", "0"):
        raise Fail("GitHub's master has new commits: git pull --rebase, then publish again")
    pushed_at = time.time()
    for s in steps:
        shown = " ".join(x if " " not in x else f'"{x}"' for x in s)
        print(("would run: " if dry else "> ") + shown)
        if dry:
            continue
        try:
            run(s, env=env)
        except Fail as e:
            if "push" in s:  # nothing went to GitHub: take the tag back, so publish can run again
                git("tag", "-d", tag, check=False)
                raise Fail(f"{e}\nThe tag {tag} is deleted here; the commit stays. Fix the cause (git pull --rebase), "
                           "then publish again.")
            raise
    if dry:
        print("dry run: nothing changed")
        return
    sha = git("rev-parse", "HEAD")
    # CI: the tag's run (its branch is the tag) made after the push, to its end. An older run of the same commit (a tag
    # pushed again after untag) does not count.
    run_id, t = None, time.time()
    while not run_id and time.time() - t < 300:
        out = run(["gh", "run", "list", "--repo", REPO, "--workflow", "release.yml", "--branch", tag, "--limit", "5",
                   "--json", "databaseId,headSha,createdAt"], env=env, check=False)
        for r in json.loads(out or "[]"):  # newest first
            made = calendar.timegm(time.strptime(r["createdAt"], "%Y-%m-%dT%H:%M:%SZ"))  # (UTC)
            if r["headSha"] == sha and made >= pushed_at - 30:
                run_id = r["databaseId"]
                break
        if not run_id:
            time.sleep(10)
    if not run_id:
        raise Fail(f"no CI run for {tag} after 5 minutes: gh run list --workflow release.yml --branch {tag}")
    print(f"CI run {run_id}: https://github.com/{REPO}/actions/runs/{run_id} (it waits for the build and tests of master's push, about 15 minutes, then releases)")
    while True:
        st = json.loads(run(["gh", "run", "view", str(run_id), "--repo", REPO, "--json", "status,conclusion"], env=env))
        if st["status"] == "completed":
            break
        time.sleep(30)
    if st["conclusion"] != "success":
        raise Fail(f"CI run {run_id}: {st['conclusion']}. gh run view {run_id} --log-failed; "
                   "docs/dev/DEVELOPING.md, \"When a release run fails\"")
    confirm(version, sha, env)


def cmd_untag(args):
    version = args[0]
    tag = f"v{version}"
    token = push_token()
    env = push_env(token)
    if run(["gh", "release", "view", tag, "--repo", REPO, "--json", "name"], env=env, check=False):
        raise Fail(f"the release {tag} is there: a release that people can have is never moved. Make a new version.")
    if git("tag", "-l", tag):
        git("tag", "-d", tag)
        print(f"deleted the tag {tag} here")
    if run(["git", "ls-remote", "--tags", "origin", tag], check=False):
        run(["git", *push_git_args(), "push", "origin", f":refs/tags/{tag}"], env=env)
        print(f"deleted the tag {tag} on GitHub")
    print(f"next: fix it on master, then publish {version} --yes")


def confirm(version, sha, env):
    tag = f"v{version}"
    rel = json.loads(run(["gh", "release", "view", tag, "--repo", REPO, "--json", "name,url,assets,body"], env=env))
    latest = run(["gh", "api", f"repos/{REPO}/releases/latest", "--jq", ".tag_name"], env=env, check=False)
    title = f"StarDraft 2 - v{version} (build {sha[:7]})"
    problems = []
    if rel["name"] != title:
        problems.append(f"the release title is \"{rel['name']}\", not \"{title}\"")
    if latest != tag:
        problems.append(f"Latest is {latest}, not {tag}")
    zips = [a for a in rel["assets"] if a["name"] == f"StarDraft-{version}-win64.zip" and a["size"] > 0]
    # Other systems' downloads may be there too. They must not end in .zip: Check for updates in
    # older builds takes a release's last .zip.
    if not zips:
        problems.append(f"the downloads are {[a['name'] for a in rel['assets']]}: no StarDraft-{version}-win64.zip")
    if "](images/" in rel["body"]:
        problems.append("the release text has picture paths that are not addresses")
    drafts = run(["gh", "release", "list", "--repo", REPO, "--limit", "50", "--json", "tagName,isDraft"], env=env, check=False)
    if not any(r["tagName"] == f"symbols-{tag}" and r["isDraft"] for r in json.loads(drafts or "[]")):
        problems.append(f"no draft release symbols-{tag} (the program's symbols for crash reports)")
    if problems:
        raise Fail("the release is out, but:\n  " + "\n  ".join(problems))
    print(f"Released: {rel['url']}")


def cmd_shots(args):
    version = args[0]
    spec_file = SHOTS / f"{version}.json"
    if not spec_file.is_file():
        raise Fail(f"no {spec_file.relative_to(ROOT)} (see tools/shots/README.md)")
    spec = json.loads(spec_file.read_text(encoding="utf-8"))
    install = Path(os.environ.get("STARCRAFT_INSTALL", r"C:\Program Files (x86)\Starcraft Remastered\StarCraft"))
    if not (install / ".build.info").is_file():
        raise Fail(f"no StarCraft install at {install} (set STARCRAFT_INSTALL)")
    build = ROOT / (opt(args, "--build-dir") or "build")
    exe = build / "Release" / "stardraft-testing.exe"  # (the testing build: the user's StarDraft 2.exe stays alone)
    if not exe.is_file():
        raise Fail(f"no {exe.relative_to(ROOT)}: cmake -S . -B build -DSTARDRAFT_EXE_NAME=stardraft-testing, "
                   "cmake --build build --config Release --target stardraft_editor")
    scratch = Path(tempfile.mkdtemp(prefix=f"stardraft-shots-{version}-"))
    captures = {}
    for i, r in enumerate(spec["runs"]):
        if "cases" not in r:
            raise Fail(f"run {i} has no cases (runs with specs were for --datashots of the old Win32 program, which "
                       "is gone; see tools/shots/README.md)")
        out = scratch / f"run{i}"
        out.mkdir()
        env = {**os.environ, **r.get("env", {})}
        # The snapshots (no window, no sound, no focus): each case of tools/sdl_snapshots.py writes <case>.png.
        cmd = [sys.executable, "-I", "tools/sdl_snapshots.py", "--exe", str(exe), "--out", str(out),
               "--install", str(install), "--only", ",".join(r["cases"])]
        if "map" in r:
            cmd += ["--map", str(install / r["map"])]
        if "project" in r:
            cmd += ["--project", str(ROOT / r["project"])]
        if r.get("cmbw"):  # (the Cosmonarchy BW cases: the mod's install, found as the program finds it; no map of it)
            cmd += ["--cmbwmap", "-"]
        t = time.time()
        p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=600 + 120 * len(r["cases"]))
        print(f"run {i} ({', '.join(r['cases'])}): {time.time() - t:.0f} s, exit {p.returncode}")
        if p.returncode:
            raise Fail(f"run {i}: " + ((p.stdout + p.stderr).strip().splitlines() or ["no output"])[-1])
        for case in r["cases"]:
            png = out / f"{case}.png"
            if not png.is_file():
                raise Fail(f"run {i}: no picture of the case {case} (it needs a map, a project or the game data)")
            captures[f"{i}/{png.name}"] = png
    if "--measure" in args:
        for name, p in sorted(captures.items()):
            print(f"  run {name}: {png_size(p)}  {p}")
        return
    try:
        from PIL import Image
    except ImportError:
        raise Fail("cropping needs Pillow: python -m pip install pillow")
    dest = IMAGES / version
    dest.mkdir(parents=True, exist_ok=True)
    for s in spec["shots"]:
        src = captures.get(f"{s.get('run', 0)}/{s['from']}")
        if not src:
            raise Fail(f"no capture {s['from']} in run {s.get('run', 0)} (captures: {', '.join(sorted(captures))})")
        im = Image.open(src)
        if "crop" in s:
            x, y, w, h = s["crop"]
            im = im.crop((x, y, x + w, y + h))
        if "scale" in s:
            im = im.resize((round(im.width * s["scale"]), round(im.height * s["scale"])), Image.LANCZOS)
        im.save(dest / s["to"])
        print(f"  docs/user/images/{version}/{s['to']}: {im.width} x {im.height}  ![{s.get('alt', '')}](images/{version}/{s['to']})")
    shutil.rmtree(scratch, ignore_errors=True)


def png_size(p):
    b = Path(p).read_bytes()[16:24]
    return f"{int.from_bytes(b[:4], 'big')} x {int.from_bytes(b[4:], 'big')}"


def opt(args, name):
    return args[args.index(name) + 1] if name in args and args.index(name) + 1 < len(args) else None


COMMANDS = {"pending": (cmd_pending, 0), "notes": (cmd_notes, 1), "discord": (cmd_discord, 1), "discord-text": (cmd_discord_text, 1), "title": (cmd_title, 1), "ci-check": (cmd_ci_check, 1),
            "check": (cmd_check, 1), "publish": (cmd_publish, 1), "shots": (cmd_shots, 1), "untag": (cmd_untag, 1)}


def main():
    for stream in (sys.stdout, sys.stderr):  # texts in other languages, also into a pipe (-I ignores PYTHONUTF8)
        stream.reconfigure(encoding="utf-8", errors="replace")
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS or len(sys.argv) - 2 < COMMANDS[sys.argv[1]][1]:
        print(__doc__)
        return 2
    try:
        COMMANDS[sys.argv[1]][0](sys.argv[2:])
    except Fail as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
