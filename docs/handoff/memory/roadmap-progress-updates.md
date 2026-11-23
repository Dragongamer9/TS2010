---
name: roadmap-progress-updates
description: TS2010 repo has ROADMAP.md with a weighted 0-100% progress figure that must be updated with every change
metadata:
  type: feedback
---

The owner (Dragongamer9) wants ROADMAP.md in the Dragongamer9/TS2010 repo kept current: overall progress 0–100%, per-milestone "done" %, and a new progress-log row every time work moves forward.

**Why:** he asked for a roadmap "with a progress update from 0 to 100% and update it every time" (2026-11-10).

**How to apply:**
- After each milestone change, recompute overall = Σ(weight×done)/Σ(weight).
- Show the arithmetic, update the bar and the "Last updated" line, and add a log row in the same commit.
- Never inflate the figure. A milestone is only 100% when merged, CI-green, documented, and run on Dad's real install.
- Steps 12–15 (script VM, train simulation, game UI, networking) are marked owner/other: Claude doesn't write them.

Related: [[ts2010-port-rules]]
