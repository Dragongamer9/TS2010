---
name: ts2010-port-rules
description: Constraints for the TS2010 Linux port project (dad's game data safety, repo visibility, what Claude writes)
metadata:
  type: project
---

Private project of Dragongamer9 and his dad, who owns the TS2010 copy. Repo: Dragongamer9/TS2010, worked on branch hearth/use-the-rea-tools-to-look-at-4c82, PR #1.

- **Dad's install is read-only.** All port output goes only under ~/.local/share/trainz-native/ (enforced by src/common/SafeOutput).
- **Game files and the repo.** The repo went PRIVATE on 2026-11-10. testdata/ts2010 now holds Base.ja, Data.ja and Builtin/sample8.ja (a subset of 8.ja built by tools/dev/make_sample_ja.py), and CI runs the RealInstall.* tests on it. If the repo ever goes public, those files must be removed from git history first.
- **Size limits.** Builtin .ja files (0.5–1.2 GB each) exceed GitHub's 100 MB limit; only Base.ja (44 MB) and Data.ja (65 MB) fit without LFS.
- **What Claude writes.** File-format readers, viewers, converters, tools, tests, CI and packaging. Claude doesn't write the TrainzScript VM, the train simulation or the game UI (no recreation of the game itself).
- **Builds.** Nothing compiles on this PC; every C++ build and test runs in GitHub Actions. Report skipped tests as skipped, not passed.

Related: [[roadmap-progress-updates]]
