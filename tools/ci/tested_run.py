"""The run of .github/workflows/build.yml that built and tested a commit, for its release (release.yml): a push run of
that commit (on a branch) whose jobs all passed, built as the release's version. Waits while such a run is not done.
Prints the run's id, or nothing when there is none (then release.yml builds the commit itself). Uses gh (GH_TOKEN).

usage: python3 -I tools/ci/tested_run.py --sha <commit> --version <version name> [--any-version] [--wait <minutes>]
    --version: the version that the run's builds must have (version_name.sh: the tag's); --any-version: a test
    (a ci-test-<n> tag), any version.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time

REPO = os.environ.get("GITHUB_REPOSITORY", "stardraft-editor/stardraft")
# The jobs whose files a release has, or that test them: each one must have passed (not skipped).
REQUIRED = {"windows", "macos", "linux-appimage", "linux", "linux-clang"}
# Not waited for: linux-data (now in data.yml; its runner is usually off); wine only reports (continue-on-error).
IGNORED = {"linux-data", "wine"}
VERSIONS = ("windows", "macos")  # (the jobs that build with the version name: build-version-<job>-<commit>)


def gh(*args):
    r = subprocess.run(["gh", *args], capture_output=True, text=True, encoding="utf-8")
    if r.returncode:
        raise RuntimeError(f"gh {' '.join(args)}: {r.stderr.strip()[:300]}")
    return r.stdout


def api(path):
    return json.loads(gh("api", path))


def state(run):
    """'good', 'wait' or 'bad', and why."""
    run = api(f"repos/{REPO}/actions/runs/{run['id']}")
    every = api(f"repos/{REPO}/actions/runs/{run['id']}/jobs?per_page=100")["jobs"]
    jobs = [j for j in every if j["name"] not in IGNORED]
    ignored_open = any(j["name"] in IGNORED and j["status"] != "completed" for j in every)
    # (a job that waits for another one may not be listed yet: the run is done when all but the ignored jobs are)
    if any(j["status"] != "completed" for j in jobs) or (run["status"] != "completed" and not ignored_open):
        return "wait", "not done"
    bad = [f"{j['name']} {j['conclusion']}" for j in jobs if j["conclusion"] not in ("success", "skipped")]
    required = REQUIRED | ({"release-test-macos"} if run["head_branch"] == "port/kit" or run["head_branch"].startswith("port/t-") else set())
    bad += [f"{n} not run" for n in sorted(required - {j["name"] for j in jobs if j["conclusion"] == "success"})]
    return ("bad", ", ".join(bad)) if bad else ("good", "all jobs passed")


def built_version(run_id, sha, job):
    folder = tempfile.mkdtemp(prefix="tested-run-")
    gh("run", "download", str(run_id), "--repo", REPO, "--name", f"build-version-{job}-{sha}", "--dir", folder)
    with open(os.path.join(folder, f"version-{job}.txt"), encoding="utf-8") as f:
        return f.read().strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sha", required=True)
    ap.add_argument("--version", default="")
    ap.add_argument("--any-version", action="store_true")
    ap.add_argument("--wait", type=float, default=100, help="minutes to wait for a run that is not done")
    a = ap.parse_args()
    deadline = time.time() + a.wait * 60
    seen = {}
    while True:
        runs = api(f"repos/{REPO}/actions/workflows/build.yml/runs?head_sha={a.sha}&event=push&per_page=30")["workflow_runs"]
        # The sha check: only runs of this very commit, pushed to a branch (not the runs of tags before release.yml).
        runs = [r for r in runs if r["head_sha"] == a.sha and not re.match(r"(v\d|ci-test-)", r["head_branch"] or "")]
        waiting = False
        for r in runs:  # (newest first)
            kind, why = state(r)
            if seen.get(r["id"]) != (kind, why):
                print(f"run {r['id']} ({r['head_branch']}, attempt {r['run_attempt']}): {kind}: {why}", file=sys.stderr)
                seen[r["id"]] = (kind, why)
            if kind == "wait":
                waiting = True
                continue
            if kind != "good":
                continue
            try:
                versions = {job: built_version(r["id"], a.sha, job) for job in VERSIONS}
            except RuntimeError as e:
                print(f"run {r['id']}: no version files ({e})", file=sys.stderr)
                continue
            if not a.any_version and any(v != a.version for v in versions.values()):
                print(f"run {r['id']}: built as {versions}, not {a.version!r}", file=sys.stderr)
                continue
            print(f"run {r['id']}: the tested build of {a.sha[:7]} (versions {versions})", file=sys.stderr)
            print(r["id"])
            return 0
        if not waiting or time.time() > deadline:
            print(f"no tested run of {a.sha[:7]}" + (" in time" if waiting else "") + ": the release builds it", file=sys.stderr)
            return 0
        time.sleep(30)


if __name__ == "__main__":
    sys.exit(main())
