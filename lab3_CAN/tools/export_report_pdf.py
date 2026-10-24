"""Export Doc/lab3/lab3_report.md to Doc/lab3/lab3_report.pdf.

    pip install markdown-it-py
    python lab3_CAN/tools/export_report_pdf.py

Renders the Markdown with markdown-it (CommonMark + tables, so our 2-space
nested lists work), wraps each image in a figure with its alt text as the
caption, and prints the page with headless Microsoft Edge (or Chrome).
In-document links (M1-M6, photo N) stay clickable in the PDF.
"""
import html
import pathlib
import re
import shutil
import subprocess
import sys

from markdown_it import MarkdownIt

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "Doc" / "lab3" / "lab3_report.md"
OUT = SRC.with_suffix(".pdf")

CSS = """
@page { size: Letter landscape; margin: 0.45in 0.5in; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { font-family: "Segoe UI", Calibri, Arial, sans-serif; font-size: 10pt;
       line-height: 1.38; color: #1d2733; margin: 0; }
h1 { font-size: 20pt; color: #17365d; margin: 0 0 4pt; }
h2 { font-size: 15pt; color: #17365d; border-bottom: 1.5pt solid #17365d;
     padding-bottom: 2pt; margin: 14pt 0 6pt; break-before: page; }
h1 + p + p + p + h2, body > h2:first-of-type { break-before: auto; }
h3 { font-size: 12pt; color: #24507f; margin: 12pt 0 4pt; break-after: avoid; }
h4 { font-size: 10.5pt; color: #24507f; margin: 10pt 0 3pt; break-after: avoid; }
p { margin: 3pt 0 5pt; }
ul, ol { margin: 2pt 0 6pt; padding-left: 18pt; }
li { margin: 1pt 0; }
li > ul, li > ol { margin: 1pt 0; }
a { color: #1f5fa8; text-decoration: none; }
code { font-family: Consolas, "Cascadia Mono", monospace; font-size: 8.8pt;
       background: #eef2f6; padding: 0 2pt; border-radius: 2pt; }
pre { background: #eef2f6; padding: 6pt; border-radius: 3pt; font-size: 8.5pt;
      white-space: pre-wrap; break-inside: avoid; }
table { border-collapse: collapse; width: 100%; margin: 4pt 0 8pt;
        font-size: 8.3pt; line-height: 1.25; }
thead { display: table-header-group; }
tr { break-inside: avoid; }
th { background: #17365d; color: #fff; text-align: left; font-weight: 600; }
th, td { border: 0.6pt solid #b4c0cc; padding: 2.5pt 4pt; vertical-align: top;
         overflow-wrap: break-word; }
th { white-space: nowrap; }
tbody tr:nth-child(even) td { background: #f3f6f9; }
figure { margin: 8pt 0 12pt; text-align: center; break-inside: avoid; }
figure img { max-width: 100%; max-height: 6.1in; object-fit: contain; }
figcaption { font-size: 8.8pt; color: #4a5a6b; margin-top: 3pt; font-style: italic; }
h4 + figure, a + h4 { break-before: avoid; }
"""


def render():
    md = MarkdownIt("commonmark", {"html": True}).enable("table")
    text = SRC.read_text(encoding="utf-8")
    text = re.sub(r"<!--.*?-->\s*", "", text, flags=re.S)
    text = re.sub(r'<link rel="stylesheet"[^>]*>\s*', "", text)
    body = md.render(text)

    # <p><img alt=..></p>  ->  <figure><img><figcaption>alt</figcaption></figure>
    def fig(m):
        img = m.group(1)
        alt = re.search(r'alt="([^"]*)"', img)
        cap = f"<figcaption>{alt.group(1)}</figcaption>" if alt and alt.group(1) else ""
        return f"<figure>{img}{cap}</figure>"

    body = re.sub(r"<p>\s*(<img [^>]*>)\s*</p>", fig, body)
    base = SRC.parent.as_uri() + "/"
    return (f'<!doctype html><html><head><meta charset="utf-8"><base href="{html.escape(base)}">'
            f"<title>18-449/649 Lab 3 Report</title><style>{CSS}</style></head>"
            f"<body>{body}</body></html>")


def browser():
    for c in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
              r"C:\Program Files\Google\Chrome\Application\chrome.exe",
              shutil.which("msedge"), shutil.which("google-chrome"), shutil.which("chromium")):
        if c and pathlib.Path(c).exists():
            return c
    sys.exit("No Edge/Chrome found for printing")


def main():
    tmp = OUT.with_name("_lab3_report_export.html")
    tmp.write_text(render(), encoding="utf-8")
    try:
        subprocess.run([browser(), "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                        "--allow-file-access-from-files", "--virtual-time-budget=10000",
                        f"--print-to-pdf={OUT}", tmp.as_uri()],
                       check=True, capture_output=True, timeout=180)
    finally:
        tmp.unlink(missing_ok=True)
    pages = len(re.findall(rb"/Type\s*/Page[^s]", OUT.read_bytes()))
    print(f"wrote {OUT} ({pages} pages)")


if __name__ == "__main__":
    main()
