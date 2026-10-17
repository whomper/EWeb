#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Fetches the URL Standard's tests (url/resources/urltestdata.json in the
# Web Platform Tests) into build/wpt, for make urltest. Only that folder is
# checked out (tests/html5lib/fetch.sh adds its own).
set -e
dir=${1:-build/wpt}
if [ ! -d "$dir/.git" ]; then
	git clone -q --depth 1 --filter=blob:none --sparse https://github.com/web-platform-tests/wpt "$dir"
fi
git -C "$dir" sparse-checkout add url/resources
echo "urltestdata.json: $(grep -c '"input"' "$dir"/url/resources/urltestdata.json) tests, WPT $(git -C "$dir" log -1 --format='%h %cs')"
