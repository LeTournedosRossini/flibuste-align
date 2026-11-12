#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fabrique dist/align-native/ : le paquet autonome de sources que DAW compile (crates/align/native/). Relancer après toute modification du cœur."""
import shutil, sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
out = root / "dist" / "align-native"
if out.exists():
    shutil.rmtree(out)
(out / "include" / "align").mkdir(parents=True)
(out / "src").mkdir()
for h in ["align_c.h", "Align.h", "Pipeline.h", "Progress.h", "Render.h", "Settings.h"]:
    shutil.copy(root / "core" / "include" / "align" / h, out / "include" / "align" / h)
sources = ["Align.cpp", "Progress.cpp", "Render.cpp", "Pipeline.cpp", "Settings.cpp", "align_c.cpp"]
for s in sources:
    shutil.copy(root / "core" / "src" / s, out / "src" / s)
tp = root / "third_party"
shutil.copytree(tp / "rubberband", out / "third_party" / "rubberband", ignore=shutil.ignore_patterns("dotnet", "cross", "otherbuilds", "vamp", "ladspa-lv2", "com", "main", "meson*", "*.md", "Doxyfile", "CHANGELOG", "CONTRIBUTING*"))
shutil.copy(tp / "rubberband" / "COPYING", out / "third_party" / "rubberband" / "COPYING")
shutil.copytree(tp / "signalsmith-stretch", out / "third_party" / "signalsmith-stretch", ignore=shutil.ignore_patterns("web", "cmd", "SUPPORT.txt", "CMakeLists.txt"))
shutil.copytree(tp / "signalsmith-linear", out / "third_party" / "signalsmith-linear", ignore=shutil.ignore_patterns("tests", "SUPPORT.txt", "CMakeLists.txt"))
(out / "demo").mkdir()
shutil.copytree(tp / "dr_wav", out / "third_party" / "dr_wav")
shutil.copy(root / "docs" / "bundle" / "align_demo.c", out / "demo" / "align_demo.c")
shutil.copy(root / "docs" / "bundle" / "CMakeLists.txt", out / "CMakeLists.txt")
shutil.copy(root / "docs" / "bundle" / "LISEZMOI.md", out / "LISEZMOI.md")
(out / "sources.txt").write_text("\n".join("src/" + s for s in sources) + "\nthird_party/rubberband/single/RubberBandSingle.cpp\n")
shutil.copytree(root / "docs" / "bundle" / "ref", out / "ref")
shutil.copy(root / "LICENSE", out / "LICENSE-GPLv3")
print("paquet prêt :", out)
