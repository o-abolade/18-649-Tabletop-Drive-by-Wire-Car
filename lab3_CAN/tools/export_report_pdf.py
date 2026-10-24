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
import tempfile

from markdown_it import MarkdownIt
from PIL import Image

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
     padding-bottom: 2pt; margin: 16pt 0 6pt; break-before: page; break-after: avoid; }
body > h2:first-of-type { break-before: auto; }
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
figure img { max-width: 100%; max-height: 6.6in; object-fit: contain; }
figure.wide img { width: 100%; max-height: 6.9in; }
figure.tall { margin: 0 0 6pt; }
figure.tall img { height: 7.1in; max-height: none; }
h2 + figure.tall img { height: 6.7in; }
figure.tall + figure.tall { break-before: page; }
.shots + h3 { break-before: page; }
.shots { display: grid; grid-template-columns: 1fr 1fr; column-gap: 16pt; }
.shot { break-inside: avoid; }
.shot h4 { font-size: 9.5pt; margin: 4pt 0 2pt; min-height: 2.6em; }
.shot figure { margin: 0 0 8pt; }
.shot figure img { max-height: 5.6in; }
figcaption { font-size: 8.8pt; color: #4a5a6b; margin-top: 3pt; font-style: italic; }
h4 + figure, a + h4 { break-before: avoid; }
"""


def shrink_photos(body, tmp):
    """Point scope photos at 2400 px copies so the PDF stays small."""
    def one(m):
        src = SRC.parent / m.group(1)
        im = Image.open(src)
        im.thumbnail((2400, 2400))
        out = tmp / src.name
        im.save(out, "JPEG", quality=85)
        return f'src="{out.as_uri()}"'
    return re.sub(r'src="(\.\./assets/lab3_scope/[^"]+\.jpe?g)"', one, body)


def render(tmp):
    md = MarkdownIt("commonmark", {"html": True}).enable("table")
    text = SRC.read_text(encoding="utf-8")
    text = re.sub(r"<!--.*?-->\s*", "", text, flags=re.S)
    text = re.sub(r'<link rel="stylesheet"[^>]*>\s*', "", text)
    body = md.render(text)

    # <p><img alt=..></p>  ->  <figure><img><figcaption>alt</figcaption></figure>
    # Diagrams: use the vector export from drawio_to_svg.py when present.
    def fig(m):
        img = m.group(1)
        src = re.search(r'src="([^"]*)"', img).group(1)
        cls = ""
        if src.endswith(".drawio.png"):
            svg = src[:-4] + ".svg"
            if (SRC.parent / svg).exists():
                img = img.replace(src, svg)
            cls = ' class="tall"' if "task_graph" in src else ' class="wide"'
        alt = re.search(r'alt="([^"]*)"', img)
        cap = f"<figcaption>{alt.group(1)}</figcaption>" if alt and alt.group(1) else ""
        return f"<figure{cls}>{img}{cap}</figure>"

    body = re.sub(r"<p>\s*(<img [^>]*>)\s*</p>", fig, body)
    body = shrink_photos(body, tmp)

    # Scope captures (anchor + heading + photo) two to a page.
    shot = r'(?:<p>(?:<a id="[^"]+"></a>)+</p>\s*)?<h4>.*?</h4>\s*<figure>.*?</figure>\s*'
    body = re.sub(f"(?:{shot})+", lambda m: '<div class="shots">' + re.sub(
        shot, lambda n: f'<div class="shot">{n.group(0)}</div>', m.group(0), flags=re.S) + "</div>",
        body, flags=re.S)
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
    with tempfile.TemporaryDirectory() as d:
        page = pathlib.Path(d) / "lab3_report.html"
        page.write_text(render(pathlib.Path(d)), encoding="utf-8")
        subprocess.run([browser(), "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                        "--allow-file-access-from-files", "--virtual-time-budget=10000",
                        f"--print-to-pdf={OUT}", page.as_uri()],
                       check=True, capture_output=True, timeout=180)
    pages = len(re.findall(rb"/Type\s*/Page[^s]", OUT.read_bytes()))
    print(f"wrote {OUT} ({pages} pages)")


if __name__ == "__main__":
    main()
