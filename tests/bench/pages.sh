#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Fetches nsbench's page set into build/pages (never committed: the pages
# and sheets belong to their authors):
#   pages/  258 real web pages, saved by the htmlparser-benchmark project
#           (npm htmlparser-benchmark 1.1.3: BBC, NYT, GitHub and others)
#   css/    two real stylesheets standing in for the pages' linked ones,
#           which the corpus doesn't keep: Bootstrap 5.3.3 (233 KB, class
#           rules) and Pico 2.0.6 (82 KB, rules on elements and attributes)
set -e
out=${1:-build/pages}
mkdir -p "$out/tmp" "$out/css"
cd "$out/tmp"
npm pack --silent htmlparser-benchmark@1.1.3 bootstrap@5.3.3 @picocss/pico@2.0.6 > /dev/null
for f in *.tgz; do mkdir -p "${f%.tgz}"; tar -xzf "$f" -C "${f%.tgz}"; done
rm -rf ../pages && mv htmlparser-benchmark-1.1.3/package/files ../pages
cp bootstrap-5.3.3/package/dist/css/bootstrap.min.css ../css/
cp picocss-pico-2.0.6/package/css/pico.min.css ../css/
cd .. && rm -rf tmp
echo "$(ls pages | wc -l) pages in $out/pages, sheets in $out/css"
