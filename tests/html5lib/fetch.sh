#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Fetches the HTML tree-construction tests (html5lib's .dat files, kept by
# the Web Platform Tests in html/syntax/parsing/resources) into build/wpt,
# for make h5test. Only that folder is checked out (tests/url/fetch.sh adds
# its own).
set -e
dir=${1:-build/wpt}
if [ ! -d "$dir/.git" ]; then
	git clone -q --depth 1 --filter=blob:none --sparse https://github.com/web-platform-tests/wpt "$dir"
fi
git -C "$dir" sparse-checkout add html/syntax/parsing/resources
echo "$(ls "$dir"/html/syntax/parsing/resources/*.dat | wc -l) test files, WPT $(git -C "$dir" log -1 --format='%h %cs')"
