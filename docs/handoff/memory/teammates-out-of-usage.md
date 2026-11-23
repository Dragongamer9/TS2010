---
name: teammates-out-of-usage
description: "Don't assign work to a Command Center teammate that has run out of usage"
metadata:
  node_type: memory
  type: feedback
  originSessionId: 1b777294-c3e9-42f7-bedb-daf517e8de46
  modified: 2026-11-12T02:58:40.859Z
---

When a teammate reports "out of usage" (Cursor did, 2026-11-12), stop handing it work; do its unfinished part myself or give it to a connected teammate that still has usage.

**Why:** the owner said "If the AI runs out of usage don't use it."

**How to apply:** before writing @Name lines, skip any teammate whose last report was an out-of-usage error, even if the status list still shows it as connected.
