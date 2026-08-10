#!/usr/bin/env python3
"""Markdown -> PDF for course report/explain docs.

Tries (in order):
  1) markdown + weasyprint
  2) markdown + xhtml2pdf
  3) stdlib HTML + reportlab simple renderer (images as placeholders)
"""
from __future__ import annotations

import html
import re
import sys
from pathlib import Path


def md_to_html(text: str, title: str) -> str:
    try:
        import markdown  # type: ignore

        body = markdown.markdown(
            text,
            extensions=["tables", "fenced_code", "nl2br", "sane_lists"],
        )
    except Exception:
        # Minimal fallback: escape + preserve newlines + light headings/code
        body = _naive_md(text)

    return f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"/>
<title>{html.escape(title)}</title>
<style>
  @page {{ size: A4; margin: 18mm; }}
  body {{ font-family: DejaVu Sans, Arial, sans-serif; font-size: 11pt; line-height: 1.35; color: #111; }}
  h1 {{ font-size: 18pt; border-bottom: 1px solid #ccc; padding-bottom: 4px; }}
  h2 {{ font-size: 14pt; margin-top: 1.2em; }}
  h3 {{ font-size: 12pt; margin-top: 1em; }}
  code, pre {{ font-family: DejaVu Sans Mono, Consolas, monospace; font-size: 9.5pt; }}
  pre {{ background: #f6f6f6; padding: 8px; overflow-x: auto; white-space: pre-wrap; }}
  table {{ border-collapse: collapse; width: 100%; margin: 0.6em 0; font-size: 10pt; }}
  th, td {{ border: 1px solid #888; padding: 4px 6px; vertical-align: top; }}
  th {{ background: #eee; }}
  img {{ max-width: 100%; }}
  blockquote {{ border-left: 3px solid #ccc; margin-left: 0; padding-left: 10px; color: #333; }}
</style></head><body>
{body}
</body></html>
"""


def _naive_md(text: str) -> str:
    lines = text.replace("\\n", "\n").split("\n")
    out: list[str] = []
    in_code = False
    in_table = False
    for line in lines:
        if line.strip().startswith("```"):
            if not in_code:
                out.append("<pre>")
                in_code = True
            else:
                out.append("</pre>")
                in_code = False
            continue
        if in_code:
            out.append(html.escape(line))
            continue
        if re.match(r"^\|.+\|$", line.strip()):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if re.match(r"^[\s|:-]+$", line):
                continue
            tag = "th" if not in_table else "td"
            if not in_table:
                out.append("<table>")
                in_table = True
            out.append("<tr>" + "".join(f"<{tag}>{html.escape(c)}</{tag}>" for c in cells) + "</tr>")
            continue
        else:
            if in_table:
                out.append("</table>")
                in_table = False
        if line.startswith("# "):
            out.append(f"<h1>{html.escape(line[2:])}</h1>")
        elif line.startswith("## "):
            out.append(f"<h2>{html.escape(line[3:])}</h2>")
        elif line.startswith("### "):
            out.append(f"<h3>{html.escape(line[4:])}</h3>")
        elif line.strip() == "":
            out.append("<br/>")
        else:
            # bold **x**
            s = html.escape(line)
            s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
            s = re.sub(r"`([^`]+)`", r"<code>\1</code>", s)
            out.append(f"<p>{s}</p>")
    if in_code:
        out.append("</pre>")
    if in_table:
        out.append("</table>")
    return "\n".join(out)


def write_pdf_weasy(html_doc: str, pdf: Path, base_url: str) -> bool:
    try:
        from weasyprint import HTML  # type: ignore

        HTML(string=html_doc, base_url=base_url).write_pdf(str(pdf))
        return True
    except Exception as exc:
        print(f"[warn] weasyprint failed: {exc}")
        return False


def write_pdf_xhtml2pdf(html_doc: str, pdf: Path) -> bool:
    try:
        from xhtml2pdf import pisa  # type: ignore

        with pdf.open("wb") as fh:
            status = pisa.CreatePDF(html_doc, dest=fh, encoding="utf-8")
        return not status.err
    except Exception as exc:
        print(f"[warn] xhtml2pdf failed: {exc}")
        return False


def write_pdf_reportlab(md_text: str, pdf: Path, title: str) -> bool:
    """Plain-text-ish PDF; always available if reportlab installed, else fail."""
    try:
        from reportlab.lib.pagesizes import A4
        from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
        from reportlab.lib.units import mm
        from reportlab.platypus import Paragraph, Preformatted, SimpleDocTemplate, Spacer

        styles = getSampleStyleSheet()
        styles.add(ParagraphStyle(name="CodeSmall", fontName="Courier", fontSize=8, leading=10))
        doc = SimpleDocTemplate(
            str(pdf),
            pagesize=A4,
            leftMargin=16 * mm,
            rightMargin=16 * mm,
            topMargin=16 * mm,
            bottomMargin=16 * mm,
            title=title,
        )
        story = []
        for raw in md_text.replace("\\n", "\n").split("\n"):
            line = raw.rstrip()
            if not line:
                story.append(Spacer(1, 4))
                continue
            if line.startswith("# "):
                story.append(Paragraph(html.escape(line[2:]), styles["Title"]))
            elif line.startswith("## "):
                story.append(Paragraph(html.escape(line[3:]), styles["Heading1"]))
            elif line.startswith("### "):
                story.append(Paragraph(html.escape(line[4:]), styles["Heading2"]))
            elif line.startswith("```") or line.startswith("    "):
                story.append(Preformatted(line, styles["CodeSmall"]))
            else:
                s = html.escape(line)
                s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
                story.append(Paragraph(s, styles["BodyText"]))
            story.append(Spacer(1, 2))
        doc.build(story)
        return True
    except Exception as exc:
        print(f"[warn] reportlab failed: {exc}")
        return False


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: md_to_pdf.py input.md output.pdf", file=sys.stderr)
        return 2
    md_path = Path(sys.argv[1]).resolve()
    pdf_path = Path(sys.argv[2]).resolve()
    text = md_path.read_text(encoding="utf-8")
    title = md_path.stem
    html_doc = md_to_html(text, title)
    base = str(md_path.parent)

    if write_pdf_weasy(html_doc, pdf_path, base):
        print(f"[ok] weasyprint -> {pdf_path}")
        return 0
    if write_pdf_xhtml2pdf(html_doc, pdf_path):
        print(f"[ok] xhtml2pdf -> {pdf_path}")
        return 0
    if write_pdf_reportlab(text, pdf_path, title):
        print(f"[ok] reportlab -> {pdf_path}")
        return 0

    print("[err] No PDF backend available. Install one of:", file=sys.stderr)
    print("  sudo apt install pandoc texlive-xetex", file=sys.stderr)
    print("  OR: .venv/bin/pip install weasyprint markdown", file=sys.stderr)
    print("  OR: .venv/bin/pip install xhtml2pdf markdown", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
