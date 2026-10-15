#!/usr/bin/env python3
"""Local OAuth popup fixture: no external requests, accounts or real credentials."""
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import sys
import json


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/zane/comments":
            assert self.headers.get("X-Figma-Token") == "fixture-token"
            data = json.dumps({"comments": [{"id": "root", "message": "Fix alignment", "parent_id": "", "user": {"handle": "Alice"}, "created_at": "2026-01-01T00:00:00Z", "resolved_at": None}, {"id": "reply", "parent_id": "root", "message": "Agreed", "user": {"handle": "Bob"}}]}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        mode = "blank" if "blank" in self.path else "close"
        if self.path.startswith("/profile"):
            role = "zane" if "role=zane" in self.path else "main"
            body = f"<title>PROFILE READY</title><script>document.cookie='presence_role={role}; path=/; SameSite=Lax';</script>"
        elif self.path.startswith("/notifications"):
            body = """<title>Notification check</title><h1>Notification check</h1><p id="status">Waiting…</p><script>
              setTimeout(async () => {
                const permission = await Notification.requestPermission();
                if (permission === 'granted') {
                  window.probe = new Notification('Figma GTK notification diagnostic', {body: 'Private test bus only'});
                  document.title = 'AUTH PASS'; document.querySelector('#status').textContent = 'Notification permission granted and notification sent.';
                } else document.title = 'Notification permission failed';
              }, 300);
            </script>"""
        elif self.path.startswith("/popup"):
            finish = "location.replace('about:blank')" if mode == "blank" else "window.close()"
            body = f"""<title>Local sign-in</title><p>Completing local sign-in…</p>
              <script>setTimeout(() => {{
                document.cookie = 'signed_in=yes; path=/; SameSite=Lax';
                if (window.opener) {{
                  window.opener.postMessage('done', location.origin);
                }}
                {finish};
              }}, 300);</script>"""
        else:
            body = f"""<title>Local sign-in check</title><h1>Home</h1><p id="status">Opening local sign-in…</p>
              <script>
                console.log('fixture loaded');
                window.addEventListener('message', event => {{
                  if (event.origin === location.origin && event.data === 'done') localStorage.setItem('handoff', 'yes');
                }});
                if (document.cookie.includes('signed_in=yes') && localStorage.getItem('handoff') === 'yes') {{
                  document.title = 'AUTH PASS'; document.querySelector('#status').textContent = 'Signed in. Opener, cookie and Home refresh passed.';
                }} else setTimeout(() => console.log('popup opened: ' + !!window.open('/popup/{mode}', 'sign-in')), 200);
              </script>"""
        data = ("<!doctype html><meta charset=utf-8><style>body{font:18px system-ui;padding:40px;background:#fafafa;color:#222}</style>" + body).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_):
        pass


server = HTTPServer(("127.0.0.1", 0), Handler)
Path(sys.argv[1]).write_text(str(server.server_port))
server.serve_forever()
