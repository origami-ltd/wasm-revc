#!/usr/bin/env python3
"""
Dev host for the browser build.

Serves the built shell, the engine, and a game install over HTTP with Range support, plus a
/ViceAssets manifest describing the install. That is the same shape the Generals port uses, and
it is what lets the page be tested without driving the folder picker by hand.

    scripts/serve-web.py [--install ~/GTAVC] [--port 8100]

COOP/COEP are set on everything: SharedArrayBuffer, and therefore streaming, needs the page to
be cross-origin isolated.
"""
import argparse, json, os, pathlib, re, sys
from http.server import HTTPServer, SimpleHTTPRequestHandler

ROOT = pathlib.Path(__file__).resolve().parent.parent
LOG_PATH = pathlib.Path("/tmp/vice-runtime.log")
WEB = ROOT / "web" / "dist"
# Anything at least this big is streamed in chunks; the rest is loaded into memory at boot.
STREAM_THRESHOLD = 4 * 1024 * 1024


def manifest(install: pathlib.Path):
    entries = []
    for path in sorted(install.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(install).as_posix()
        entries.append({"path": rel, "size": path.stat().st_size})
    return entries


class Handler(SimpleHTTPRequestHandler):
    install: pathlib.Path

    def isolate(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
        # reVC.js and reVC.wasm are a matched pair — the wasm refers to EM_ASM/EM_ASYNC_JS
        # bodies in the glue by address. Serving a cached .js beside a rebuilt .wasm aborts with
        # "No EM_ASM constant found at address N", which reads like a code bug and is not one.
        self.send_header("Cache-Control", "no-store, must-revalidate")

    def end_headers(self):
        self.isolate()
        super().end_headers()

    def do_POST(self):
        # The page ships its runtime log here so it can be tailed from a terminal. A browser
        # console cannot be read from outside the browser, and that is where every useful
        # symptom shows up first.
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length)
        if self.path.split("?")[0] == "/ViceLog":
            with open(LOG_PATH, "ab") as handle:
                handle.write(body)
                handle.flush()
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path.split("?")[0] == "/ViceAssets":
            body = json.dumps({
                "entries": manifest(self.install),
                "streamThreshold": STREAM_THRESHOLD,
            }).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.startswith("/game/"):
            self.serve_game(self.path[len("/game/"):].split("?")[0])
            return
        super().do_GET()

    def serve_game(self, rel: str):
        target = (self.install / rel).resolve()
        if not str(target).startswith(str(self.install.resolve())) or not target.is_file():
            self.send_error(404)
            return
        size = target.stat().st_size
        start, end = 0, size - 1
        status = 200
        # Range is what makes on-demand streaming of a 327 MB archive possible at all.
        header = self.headers.get("Range")
        if header:
            match = re.match(r"bytes=(\d+)-(\d*)", header)
            if match:
                start = int(match.group(1))
                end = int(match.group(2)) if match.group(2) else size - 1
                end = min(end, size - 1)
                status = 206
        length = max(0, end - start + 1)
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        with target.open("rb") as handle:
            handle.seek(start)
            remaining = length
            while remaining > 0:
                chunk = handle.read(min(1 << 20, remaining))
                if not chunk:
                    break
                self.wfile.write(chunk)
                remaining -= len(chunk)

    def log_message(self, *args):
        pass  # a boot pulls thousands of ranges; the noise hides everything useful


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--install", default="~/GTAVC")
    parser.add_argument("--port", type=int, default=8100)
    args = parser.parse_args()

    install = pathlib.Path(os.path.expanduser(args.install))
    if not (install / "models").is_dir():
        sys.exit(f"no game install at {install} (expected models/gta3.img)")
    if not (WEB / "index.html").is_file():
        sys.exit(f"no built shell at {WEB} — run the shell build from the monorepo first")

    Handler.install = install
    os.chdir(WEB)
    print(f"shell   {WEB}")
    print(f"install {install} ({len(manifest(install))} files)")
    LOG_PATH.write_bytes(b"")
    print(f"http://localhost:{args.port}/?install=server")
    print(f"runtime log  tail -f {LOG_PATH}")
    HTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
