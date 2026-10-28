#!/usr/bin/env python3
"""Render docs/dos-compatibility.md from docs/compatibility/database.json.

The JSON file is the index of tested programs and their red/yellow/green
status.  The detailed per-application notes and evidence links live in
docs/compatibility/details.md.

Usage: tools/render_dos_compatibility.py [--check]

--check exits non-zero when the generated page differs from the committed
page, without writing anything.
"""
import argparse
import difflib
import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATABASE = ROOT / "docs" / "compatibility" / "database.json"
OUTPUT = ROOT / "docs" / "dos-compatibility.md"

COLORS = {
    "Working": "🟢",
    "Partial": "🟡",
    "Broken": "🔴",
    "Unsupported": "🔴",
    "Untested": "⚪",
}
ORDER = ("Working", "Partial", "Broken", "Unsupported", "Untested")


def cell(value):
    """Make a value safe inside a Markdown table cell."""
    return " ".join(str(value).split()).replace("|", "\\|")


def evidence_label(filename):
    suffix = Path(filename).suffix.lower()
    if suffix == ".png":
        return "screenshot"
    if suffix in (".txt", ".json"):
        return "transcript"
    return "evidence"


def evidence_link(filename, prefix):
    if not filename:
        return ""
    return "[%s](%s%s)" % (evidence_label(filename), prefix, filename)


def slug(heading):
    """GitHub-style heading anchor, sufficient for the headings used here."""
    text = heading.strip().lower()
    text = re.sub(r"[^\w\s-]", "", text)
    return re.sub(r"\s", "-", text)


def load_database():
    with DATABASE.open(encoding="utf-8") as handle:
        data = json.load(handle)

    if data.get("schema") != 1:
        raise SystemExit("database.json: unsupported schema")
    apps = data.get("apps")
    if not isinstance(apps, list) or not apps:
        raise SystemExit("database.json: no apps")

    details_name = data.get("details")
    if not details_name:
        raise SystemExit("database.json: no details file")
    details_path = DATABASE.parent / details_name
    if not details_path.is_file():
        raise SystemExit("database.json: details file not found: %s" % details_name)
    anchors = {
        slug(match.group(1))
        for match in re.finditer(
            r"^#{1,6}\s+(.*)$",
            details_path.read_text(encoding="utf-8"),
            re.MULTILINE,
        )
    }

    required = ("id", "name", "version", "status", "summary", "anchor",
                "evidence", "last_tested")
    seen = set()
    for index, app in enumerate(apps):
        missing = [key for key in required if not app.get(key)]
        if missing:
            raise SystemExit(
                "database.json: entry %d is missing %s"
                % (index, ", ".join(missing))
            )
        if app["status"] not in COLORS:
            raise SystemExit(
                "database.json: %s has unknown status %r"
                % (app["name"], app["status"])
            )
        if app["anchor"] not in anchors:
            raise SystemExit(
                "database.json: %s anchor not found in %s: #%s"
                % (app["name"], details_name, app["anchor"])
            )
        if app["id"] in seen:
            raise SystemExit("database.json: duplicate id %r" % app["id"])
        seen.add(app["id"])
        if not (ROOT / "docs" / "compatibility" / app["evidence"]).is_file():
            raise SystemExit(
                "database.json: %s evidence file not found: %s"
                % (app["name"], app["evidence"])
            )
    return data


def table_row(app, include_status=False, prefix="compatibility/",
              details="details.md"):
    name = "[%s](%s%s#%s)" % (
        cell(app["name"]),
        prefix,
        details,
        app["anchor"],
    )
    values = [name, cell(app["version"])]
    if include_status:
        values.append("%s **%s**" % (COLORS[app["status"]], app["status"]))
    values.extend([
        cell(app["summary"]),
        evidence_link(app["evidence"], prefix),
        cell(app["last_tested"]),
    ])
    return "| " + " | ".join(values) + " |"


def render(data):
    apps = data["apps"]
    details = data["details"]
    counts = Counter(app["status"] for app in apps)
    red = counts["Broken"] + counts["Unsupported"]
    working_partial = counts["Working"] + counts["Partial"]
    tested_pct = 100.0 * working_partial / len(apps)

    lines = [
        "# DOS software compatibility",
        "",
        "Last updated: **%s**. ManiOS version: **%s**."
        % (data["updated"], data["build"]["version"]),
        "",
        "**%d programs:** %s **%d Working** · %s **%d Partial** · %s **%d Broken/unsupported** · %s **%d Untested**"
        % (
            len(apps),
            COLORS["Working"], counts["Working"],
            COLORS["Partial"], counts["Partial"],
            COLORS["Broken"], red,
            COLORS["Untested"], counts["Untested"],
        ),
        "",
        "**Working + Partial: %d / %d programs (%.1f%%).**"
        % (working_partial, len(apps), tested_pct),
        "",
        "This percentage describes catalog entries whose tested task works at",
        "least partially; it does not claim that every feature of those programs",
        "works. The full test history is in",
        "[%s](compatibility/%s); the machine-readable index is"
        % (details, details),
        "[database.json](compatibility/database.json).",
        "",
        "| Colour | Status | Meaning |",
        "| --- | --- | --- |",
        "| 🟢 | Working | The recorded task completed with usable output. |",
        "| 🟡 | Partial | Some meaningful functions work; the normal workflow is incomplete or unverified. |",
        "| 🔴 | Broken | The program runs but fails or hangs before completing a basic workflow. |",
        "| 🔴 | Unsupported | A required CPU mode, subsystem or interface is missing. |",
        "| ⚪ | Untested | No current result. |",
        "",
    ]

    sections = (
        ("Working", "🟢 Working", "Verified task", False,
         [app for app in apps if app["status"] == "Working"]),
        ("Partial", "🟡 Partial", "Partial result", False,
         [app for app in apps if app["status"] == "Partial"]),
        ("Red", "🔴 Broken and unsupported", "Blocker", True,
         [app for app in apps
          if app["status"] in ("Broken", "Unsupported")]),
    )

    for _status, heading, result_heading, include_status, group in sections:
        group.sort(key=lambda app: app["name"].casefold())
        lines.append("## %s (%d)" % (heading, len(group)))
        lines.append("")
        if group:
            if include_status:
                lines.append(
                    "| Application | Version / media | Status | %s | Evidence | Last tested |"
                    % result_heading
                )
                lines.append("| --- | --- | --- | --- | --- | --- |")
            else:
                lines.append(
                    "| Application | Version / media | %s | Evidence | Last tested |"
                    % result_heading
                )
                lines.append("| --- | --- | --- | --- | --- |")
            for app in group:
                lines.append(table_row(app, include_status, details=details))
        else:
            lines.append("None currently.")
        lines.append("")

    untested = [app for app in apps if app["status"] == "Untested"]
    untested.sort(key=lambda app: app["name"].casefold())
    lines.append("## ⚪ Untested (%d)" % len(untested))
    lines.append("")
    if untested:
        lines.append("| Application | Version / media | Evidence | Last tested |")
        lines.append("| --- | --- | --- | --- |")
        for app in untested:
            lines.append(
                "| [%s](compatibility/%s#%s) | %s | %s | %s |"
                % (
                    cell(app["name"]),
                    details,
                    app["anchor"],
                    cell(app["version"]),
                    evidence_link(app["evidence"], "compatibility/"),
                    cell(app["last_tested"]),
                )
            )
    else:
        lines.append("No entries are currently untested.")
    lines.append("")

    build = data["build"]
    lines.extend([
        "## Scope and method",
        "",
        "Tested against ManiOS %s (`%s`, kernel `%s`). Environment: %s."
        % (
            build["version"],
            build["commit"],
            build["kernel_sha256"],
            build["environment"],
        ),
        "",
        "Applications are started with `dosrun` from a fresh disposable FAT16",
        "image per case. Transcripts and screenshots are stored in",
        "[compatibility/](compatibility/); binaries and test disk images are not",
        "committed. See [dosrun.md](dosrun.md) for the runner and",
        "[%s](compatibility/%s) for the per-application records."
        % (details, details),
        "",
        "## Maintaining this page",
        "",
        "`docs/compatibility/database.json` is the index. To add or update a result:",
        "",
        "1. Add or edit the entry in `docs/compatibility/database.json`.",
        "2. Add the transcript or screenshot to `docs/compatibility/` and the",
        "   per-application notes to `docs/compatibility/%s`." % details,
        "3. Regenerate this page with",
        "   `python3 tools/render_dos_compatibility.py`.",
        "4. Run `python3 tools/render_dos_compatibility.py --check` to verify it is",
        "   current.",
        "",
    ])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail if the committed page is out of date",
    )
    args = parser.parse_args()

    data = load_database()
    rendered = render(data)
    current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""

    if args.check:
        if current == rendered:
            print("%s is up to date" % OUTPUT.relative_to(ROOT))
            return 0
        diff = list(difflib.unified_diff(
            current.splitlines(),
            rendered.splitlines(),
            fromfile=str(OUTPUT.relative_to(ROOT)),
            tofile="generated",
            lineterm="",
        ))
        print("\n".join(diff[:80]), file=sys.stderr)
        print("%s is out of date" % OUTPUT.relative_to(ROOT), file=sys.stderr)
        return 1

    OUTPUT.write_text(rendered, encoding="utf-8")
    print("wrote %s" % OUTPUT.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
