"""Render Doc/lab3/*.drawio to Doc/assets/<name>.drawio.svg (vector figures).

    python lab3_CAN/tools/drawio_to_svg.py

Loads each diagram in the diagrams.net viewer inside headless Edge (needs
network for viewer-static.min.js), serializes the rendered SVG and adds a
viewBox so it scales. export_report_pdf.py uses the SVG over the PNG.
"""
import html
import json
import pathlib
import re
import subprocess
import tempfile

from export_report_pdf import ROOT, browser

SCRIPT = """
<script>
function grab() {
  var s = document.querySelector('.mxgraph svg');
  if (!s) return setTimeout(grab, 200);
  var w = parseFloat(s.style.minWidth), h = parseFloat(s.style.minHeight);
  s.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
  s.setAttribute('viewBox', '0 0 ' + w + ' ' + h);
  s.setAttribute('width', w); s.setAttribute('height', h);
  s.removeAttribute('style');
  s.setAttribute('style', 'background-color:#ffffff;color-scheme:light');
  var out = new XMLSerializer().serializeToString(s);
  document.body.innerHTML = '';
  var t = document.createElement('textarea'); t.id = 'out'; t.textContent = out;
  document.body.appendChild(t);
}
window.addEventListener('load', function () { setTimeout(grab, 500); });
</script>"""


def main():
    for src in sorted((ROOT / "Doc" / "lab3").glob("*.drawio")):
        cfg = {"highlight": "none", "nav": False, "resize": False, "toolbar": "",
               "lightbox": False, "xml": src.read_text(encoding="utf-8")}
        with tempfile.TemporaryDirectory() as tmp:
            page = pathlib.Path(tmp) / "render.html"
            page.write_text(
                '<!doctype html><html><head><meta charset="utf-8"></head><body>'
                f'<div class="mxgraph" data-mxgraph="{html.escape(json.dumps(cfg))}"></div>'
                '<script src="https://viewer.diagrams.net/js/viewer-static.min.js"></script>'
                f"{SCRIPT}</body></html>", encoding="utf-8")
            dom = subprocess.run([browser(), "--headless=new", "--disable-gpu",
                                  "--virtual-time-budget=20000", "--dump-dom", page.as_uri()],
                                 capture_output=True, timeout=180).stdout.decode("utf-8")
        m = re.search(r'<textarea id="out">(.*?)</textarea>', dom, re.S)
        if not m:
            raise SystemExit(f"{src.name}: diagram did not render")
        svg = html.unescape(m.group(1))
        out = ROOT / "Doc" / "assets" / f"{src.stem}.drawio.svg"
        out.write_text(svg, encoding="utf-8")
        print(f"wrote {out.relative_to(ROOT)} ({len(svg) // 1024} KiB)")


if __name__ == "__main__":
    main()
