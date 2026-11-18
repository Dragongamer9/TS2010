"""Turn a Claude Code chat transcript (.jsonl) into readable Markdown.

Keeps the owner's messages and Claude's replies; tool calls become one
line each (tool name and its short description), tool output is left out.
Usage: python3 -I export_chat.py TRANSCRIPT.jsonl OUT.md
"""
import json
import sys


def text_of(content):
    if isinstance(content, str):
        return [("text", content)]
    out = []
    for part in content or []:
        kind = part.get("type")
        if kind == "text":
            out.append(("text", part.get("text", "")))
        elif kind == "tool_use":
            args = part.get("input", {})
            label = args.get("description") or args.get("file_path") or ""
            out.append(("tool", f"{part.get('name')}: {label}".strip()))
    return out


def main(src, dst):
    lines = []
    for raw in open(src, encoding="utf-8"):
        try:
            entry = json.loads(raw)
        except ValueError:
            continue
        kind = entry.get("type")
        if kind not in ("user", "assistant") or entry.get("isSidechain"):
            continue
        msg = entry.get("message") or {}
        stamp = (entry.get("timestamp") or "")[:16].replace("T", " ")
        for part_kind, body in text_of(msg.get("content")):
            if part_kind == "tool":
                lines.append(f"- _tool_ {body}")
                continue
            body = body.strip()
            if not body:
                continue
            if kind == "user":
                if body.startswith("<system-reminder>") or body.startswith("<local-command"):
                    continue
                lines.append(f"\n## To Claude: owner, teammate or app ({stamp})\n\n{body}\n")
            else:
                lines.append(f"\n### Claude ({stamp})\n\n{body}\n")
    with open(dst, "w", encoding="utf-8") as out:
        out.write("# Chat history\n\nExported from the Claude Code transcript. "
                  "Tool output is left out; each tool call is one line.\n")
        out.write("\n".join(lines))
        out.write("\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
