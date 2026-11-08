#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""webgate (EWeb's gateway/ copy, or WEBGATE) with translategate's local
socket at SOCK, a stand-in's (tests/browse/fakexl.py /SOCK): for
tests/browse/xlate.py and the Hatari rig's translate group.

xlgate.py SOCK WEBGATE [webgate's arguments]"""
import importlib.util
import os
import sys

sock, path = sys.argv[1], sys.argv[2]
sys.path.insert(0, os.path.dirname(os.path.abspath(path)))
spec = importlib.util.spec_from_file_location("webgate", path)
wg = importlib.util.module_from_spec(spec)
sys.argv = [path] + sys.argv[3:]
spec.loader.exec_module(wg)
wg.Config.translate_socket = sock
wg.main()
