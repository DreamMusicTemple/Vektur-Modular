#!/usr/bin/env python3
"""bundle_web.py — packs the web test build into ONE self-contained file,
web/dist/index.html: style, UI, engine, AudioWorklet source, parameter,
panel and preset tables, brand images and the WebAssembly kernel (base64).
Open it from disk or any host. Run tools/build_web.sh first."""
import base64, json, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = os.path.join(ROOT, "web")
rd = lambda n: open(os.path.join(W, n), encoding="utf-8").read()
b64 = lambda n: base64.b64encode(open(os.path.join(W, n), "rb").read()).decode()

engine = re.sub(r"^export ", "", rd("engine.js"), flags=re.M)
worklet = engine + "\n" + re.sub(r"^import .*?;\n", "", rd("worklet.js"), flags=re.M)
app = re.sub(r"^import .*?from './engine.js';\n", "", rd("app.js"), flags=re.M)
bundle = {"params": json.loads(rd("params.json")), "panels": json.loads(rd("panels.json")), "presets": json.loads(rd("presets.json")), "manual": json.loads(rd("manual.json")),
          "wasm": b64("kernel.wasm"), "worklet": worklet}
data = json.dumps(bundle, separators=(",", ":"), ensure_ascii=False).replace("</", "<\\/")
html = rd("index.html")
tag = '<script type="module" src="app.js"></script>'
assert tag in html
html = html.replace(tag, f"<script>window.VK_BUNDLE={data};</script>\n<script type=\"module\">\n{engine}\n{app}\n</script>")
for logo in ("logo-96.png", "logo-256.png"):
    html = html.replace(f'src="{logo}"', f'src="data:image/png;base64,{b64(logo)}"')
os.makedirs(os.path.join(W, "dist"), exist_ok=True)
out = os.path.join(W, "dist", "index.html")
open(out, "w", encoding="utf-8").write(html)
print(f"wrote web/dist/index.html ({os.path.getsize(out) // 1024} KB)")
