"""Attach a DAP debugger (netcoredbg) to a running Bidulgi editor and check that a C# breakpoint is hit (queue 4-5).

This speaks the Debug Adapter Protocol, the same one VS Code uses, so it doubles as a way for AI sessions (no IDE)
to stop in C# script code, read the stack and a variable, then let the game go on.

    python Scripting/Tools/dap_attach.py --debugger C:/tools/netcoredbg/netcoredbg.exe --pid 1234 \
        --source Scripting/Bidulgi.Samples/Samples.cs --line 25 --variable speed

Exit code 0 when the breakpoint was hit at that line within --timeout seconds, 1 otherwise. The editor keeps running
after the script detaches. See Docs/features/csharp-debugging.md.
"""
import argparse
import json
import os
import queue
import subprocess
import sys
import threading
import time


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--debugger", required=True, help="netcoredbg.exe")
    ap.add_argument("--pid", type=int, required=True, help="process id of BidulgiEditor.exe")
    ap.add_argument("--source", required=True, help="C# file of the breakpoint")
    ap.add_argument("--line", type=int, required=True)
    ap.add_argument("--variable", help="a field of `this` or a local to print when stopped")
    ap.add_argument("--timeout", type=float, default=90)
    ap.add_argument("--just-my-code", action="store_true", help="skip code that is not yours (default: off)")
    a = ap.parse_args()
    source = os.path.abspath(a.source)

    dbg = subprocess.Popen([a.debugger, "--interpreter=vscode"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    inbox: "queue.Queue[dict]" = queue.Queue()

    def reader():
        f = dbg.stdout
        while True:
            header = b""
            while not header.endswith(b"\r\n\r\n"):
                ch = f.read(1)
                if not ch:
                    return
                header += ch
            size = int([h for h in header.decode().split("\r\n") if h.lower().startswith("content-length")][0].split(":")[1])
            inbox.put(json.loads(f.read(size)))

    threading.Thread(target=reader, daemon=True).start()
    seq = [0]

    def wait(pred, timeout):
        end = time.time() + timeout
        while time.time() < end:
            try:
                m = inbox.get(timeout=0.5)
            except queue.Empty:
                continue
            if m.get("type") == "event" and m.get("event") in ("stopped", "breakpoint", "exited", "terminated"):
                print("event:", m["event"], json.dumps(m.get("body", {}))[:200])
            if pred(m):
                return m
        return None

    def request(cmd, args=None, timeout=60):
        seq[0] += 1
        body = json.dumps({"seq": seq[0], "type": "request", "command": cmd, "arguments": args or {}}).encode()
        dbg.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        dbg.stdin.flush()
        s = seq[0]
        r = wait(lambda m: m.get("type") == "response" and m.get("request_seq") == s, timeout)
        if not r or not r.get("success"):
            print(f"{cmd}: failed {r.get('message') if r else '(no answer)'}")
        return r

    request("initialize", {"clientID": "bidulgi", "adapterID": "coreclr", "linesStartAt1": True, "columnsStartAt1": True, "pathFormat": "path"})
    request("attach", {"processId": a.pid, "justMyCode": a.just_my_code})
    wait(lambda m: m.get("type") == "event" and m.get("event") == "initialized", 30)
    r = request("setBreakpoints", {"source": {"path": source}, "breakpoints": [{"line": a.line}]})
    if r:
        print("breakpoint:", json.dumps(r["body"]["breakpoints"]))
    request("configurationDone")
    stopped = wait(lambda m: m.get("type") == "event" and m.get("event") in ("stopped", "exited", "terminated"), a.timeout)
    hit = False
    if stopped and stopped.get("event") == "stopped":
        tid = stopped["body"].get("threadId")
        st = request("stackTrace", {"threadId": tid, "levels": 5})
        frames = st["body"]["stackFrames"] if st else []
        for f in frames:
            print("frame:", f.get("name"), (f.get("source") or {}).get("path") or "", f.get("line"))
        hit = bool(frames) and frames[0].get("line") == a.line
        if a.variable and frames:
            sc = request("scopes", {"frameId": frames[0]["id"]})
            for scope in sc["body"]["scopes"] if sc else []:
                vs = request("variables", {"variablesReference": scope["variablesReference"]})
                for v in vs["body"]["variables"] if vs else []:
                    if v["name"] == a.variable:
                        print(f"variable: {v['name']} = {v.get('value')}")
                    if v["name"] == "this" and v.get("variablesReference"):
                        inner = request("variables", {"variablesReference": v["variablesReference"]})
                        for iv in inner["body"]["variables"] if inner else []:
                            if iv["name"] == a.variable:
                                print(f"variable: this.{iv['name']} = {iv.get('value')}")
        request("setBreakpoints", {"source": {"path": source}, "breakpoints": []})
        request("continue", {"threadId": tid})
    request("disconnect", {"terminateDebuggee": False}, 10)
    dbg.kill()
    print("dap check:", f"hit line {a.line}" if hit else "breakpoint not hit")
    return 0 if hit else 1


if __name__ == "__main__":
    sys.exit(main())
