#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""Start a release: count the version in VERSION up and say what to do next.

A build never changes VERSION (it shows it as it is); only this does.
The release workflow (.github/workflows/release.yml), run by hand on main
or started by a tag vX.Y, builds the programs and publishes them as a
GitHub Release.

    tools/bump.py          1.32 -> 1.33
    tools/bump.py major    1.32 -> 2.0
"""
import sys

major, minor = open("VERSION").read().strip().split(".")[:2]
if sys.argv[1:] == ["major"]:
    version = "%d.0" % (int(major) + 1)
elif sys.argv[1:] in ([], ["minor"]):
    version = "%s.%d" % (major, int(minor) + 1)
else:
    sys.exit(__doc__)
open("VERSION", "w").write(version + "\n")
print("VERSION is now %s. Then:" % version)
print('  git commit -am "Release %s" && git push' % version)
print("  and run the Release workflow on main (Actions > Release > Run workflow)")
print('  with what is new; or: git tag -a v%s -m "What is new" && git push origin v%s'
      % (version, version))
