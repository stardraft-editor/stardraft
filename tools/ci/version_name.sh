#!/bin/bash
# The version name of a CI build (-DSTARDRAFT_VERSION_NAME), or nothing (then the build keeps the version of
# CMakeLists.txt). A release tag's build (v0.5.0.1, v0.6.0-preview.1): the tag's version. A push of a commit that has a
# release tag on GitHub (release.py publish pushes main and the tag together; a preview: push the branch and the tag
# together): that tag's version, so that this run's files are the release's files and the tag's run
# (.github/workflows/release.yml) takes them as they are. A commit with no tag, a pull request, a run by hand: nothing.
#
# usage: tools/ci/version_name.sh   (in the checkout; reads GITHUB_REF, GITHUB_REF_NAME, GITHUB_EVENT_NAME, GITHUB_SHA)

set -euo pipefail
case "${GITHUB_REF:-}" in
    refs/tags/v*) echo "${GITHUB_REF_NAME#v}"; exit 0 ;;
esac
[ "${GITHUB_EVENT_NAME:-}" = push ] || exit 0
# The tags of this commit (an annotated tag's line ends in ^{}: its commit), release tags only.
tags=$(git ls-remote --tags origin 'v*' | awk -v sha="$GITHUB_SHA" '$1 == sha { t = $2; sub("^refs/tags/", "", t); sub("\\^\\{\\}$", "", t); print t }' |
       grep -E '^v[0-9]+(\.[0-9]+){1,3}(-preview\.[0-9]+)?$' | sort -u || true)
[ -n "$tags" ] || exit 0
# Two tags on one commit (a preview and then the release): the release's.
tag=$(echo "$tags" | grep -v -- '-preview' | tail -n 1 || true)
[ -n "$tag" ] || tag=$(echo "$tags" | sort -V | tail -n 1)
echo "${tag#v}"
