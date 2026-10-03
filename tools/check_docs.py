"""Check local Markdown links and optionally compile/run examples or render diagrams."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from html import escape
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import unquote, urlsplit


def fences(path):
    active, lines = None, []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("```"):
            if active is None:
                active, lines = line[3:].strip(), []
            elif line.strip() == "```":
                yield active, "\n".join(lines) + "\n"
                active = None
            else:
                raise ValueError(f"nested/unexpected fence: {path}")
        elif active is not None:
            lines.append(line)
    if active is not None:
        raise ValueError(f"unclosed fence: {path}")


def check_links(paths):
    count = 0
    for path in paths:
        # Ignore fenced source: only prose Markdown links are repository navigation.
        text = re.sub(r"^```[^\n]*\n.*?^```\s*$", "", path.read_text(encoding="utf-8"),
                      flags=re.MULTILINE | re.DOTALL)
        for destination in re.findall(r"\[[^\]\n]+\]\(([^)\n]+)\)", text):
            uri = urlsplit(destination.strip().strip("<>"))
            if uri.scheme or uri.netloc:
                continue
            target = path.parent / unquote(uri.path) if uri.path else path
            if not target.exists():
                raise ValueError(f"broken local link in {path}: {destination}")
            count += 1
    return count


def compile_examples(examples, args):
    if not args.library or not args.include:
        raise ValueError("example compilation requires --library and --include")
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name, body in examples.items():
        source, executable = output / f"{name}.cpp", output / name
        source.write_text(body, encoding="utf-8")
        command = [args.compiler, "-std=c++20", "-Wall", "-Wextra", "-Wpedantic",
                   "-Werror", "-pthread", "-I", str(Path(args.include).resolve()),
                   *args.compile_option, str(source), str(Path(args.library).resolve()),
                   "-o", str(executable)]
        if args.atomic:
            command.append("-latomic")
        subprocess.run(command, check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)
        print(f"Compiled and ran {name}", flush=True)


def render_diagram(item, output, javascript, browser):
    name, body = item
    source = json.dumps(body).replace("</", "<\\/")
    page = output / f"{name}.html"
    page.write_text(f'''<!doctype html><html><head><meta charset="utf-8">
<style>body{{margin:16px;background:white}}svg{{max-width:none!important}}</style>
<script src="{escape(javascript.as_uri(), quote=True)}"></script></head><body>
<div id="canvas"></div><pre id="renderStatus">pending</pre><script>
mermaid.initialize({{startOnLoad:false,securityLevel:'strict',theme:'neutral'}});
mermaid.render('diagramOutput', {source}).then(result => {{
  document.getElementById('canvas').innerHTML=result.svg;
  const svg=document.getElementById('canvas').querySelector('svg');
  svg.setAttribute('width',svg.viewBox.baseVal.width);
  svg.setAttribute('height',svg.viewBox.baseVal.height);
  const status=document.getElementById('renderStatus');
  status.textContent='ok';status.hidden=true;
}}).catch(error => {{document.getElementById('renderStatus').textContent=String(error)}});
</script></body></html>''', encoding="utf-8")
    # Fresh task-owned profile, not the user's browser session. No network hosts
    # resolve: the source and all rendering resources are local files.
    with tempfile.TemporaryDirectory(prefix="chrome-", dir=output) as profile:
        command = [browser, "--headless=new", "--no-sandbox", "--disable-gpu",
                   "--disable-background-networking", "--no-first-run",
                   "--host-resolver-rules=MAP * ~NOTFOUND", f"--user-data-dir={profile}",
                   "--virtual-time-budget=10000"]
        result = subprocess.run([*command, "--dump-dom", page.as_uri()],
                                capture_output=True, text=True, check=True, timeout=45)
        if not re.search(r'<pre id="renderStatus"[^>]*>ok</pre>', result.stdout):
            status = re.search(r'<pre id="renderStatus"[^>]*>(.*?)</pre>',
                               result.stdout, re.DOTALL)
            raise ValueError(f"diagram render failed: {name}: "
                             f"{status.group(1) if status else result.stderr[-1000:]}")
        svg = re.search(r"<svg\b.*?</svg>", result.stdout, re.DOTALL)
        if not svg:
            raise ValueError(f"renderer produced no SVG: {name}")
        (output / f"{name}.svg").write_text(svg.group(0), encoding="utf-8")
        viewbox = re.search(r'viewBox="([0-9. -]+)"', svg.group(0))
        if not viewbox:
            raise ValueError(f"missing SVG dimensions: {name}")
        _, _, width, height = map(float, viewbox.group(1).split())
        # Chrome's minimum window height can clip very short screenshots.
        viewport = f"{math.ceil(width) + 64},{max(600, math.ceil(height) + 64)}"
        subprocess.run([*command, f"--window-size={viewport}",
                        f"--screenshot={output / (name + '.png')}", page.as_uri()],
                       capture_output=True, text=True, check=True, timeout=45)
    print(f"Rendered locally {name}: {viewport}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--compile-examples", action="store_true")
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--library")
    parser.add_argument("--include")
    parser.add_argument("--atomic", action="store_true")
    parser.add_argument("--compile-option", action="append", default=[])
    parser.add_argument("--output", default="build/doc-examples")
    parser.add_argument("--render-diagrams", action="store_true",
                        help="render with isolated local headless Chrome; no source upload")
    parser.add_argument("--mermaid-js", help="local Mermaid browser bundle")
    parser.add_argument("--browser", default=shutil.which("google-chrome") or
                        shutil.which("chromium") or "google-chrome")
    parser.add_argument("--diagram-output", default="build/doc-diagrams")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    paths = [root / "README.md", *sorted((root / "docs").glob("*.md")),
             *sorted((root / "benchmarks").glob("*.md"))]
    examples, diagrams = {}, {}
    for path in paths:
        diagram_number = 0
        for language, body in fences(path):
            if language == "cpp":
                marker = re.match(r"// runnable: ([a-z][a-z0-9_]+)\n", body)
                if marker:
                    name = marker.group(1)
                    if name in examples or "int main(" not in body:
                        raise ValueError(f"duplicate/incomplete runnable example: {name}")
                    examples[name] = body
            elif language == "mermaid":
                diagram_number += 1
                name = "-".join(path.relative_to(root).with_suffix("").parts)
                diagrams[f"{name}-{diagram_number}"] = body
    links = check_links(paths)
    if args.compile_examples:
        if not examples:
            raise ValueError("no runnable examples found")
        compile_examples(examples, args)
    if args.render_diagrams:
        if not args.mermaid_js or not Path(args.mermaid_js).is_file():
            raise ValueError("local diagram rendering requires --mermaid-js FILE")
        javascript = Path(args.mermaid_js).resolve()
        output = Path(args.diagram_output).resolve()
        output.mkdir(parents=True, exist_ok=True)
        with ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(lambda item: render_diagram(item, output, javascript, args.browser),
                          diagrams.items()))
    print(f"Docs checked: {len(paths)} pages, {links} local links, "
          f"{len(examples)} runnable examples, {len(diagrams)} diagrams")


if __name__ == "__main__":
    main()
