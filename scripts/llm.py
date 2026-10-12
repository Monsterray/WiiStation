#!/usr/bin/env python3
"""llm.py -- call the ai-server local-llm MCP server from WiiStation scripts.

  llm.py --list                      list the server's tools (name, first description line)
  llm.py --schema TOOL               print one tool's description and argument schema
  llm.py TOOL [--arg K=V]... [--file K=PATH]... [--timeout S]

  --arg K=V      argument value. Coerced to the schema's type: integer/number/boolean parsed,
                 array (comma list or JSON) and object (JSON) parsed; string stays as given.
  --file K=PATH  argument value read from a local file (UTF-8, no size cap). PATH '-' reads stdin.
                 Use this for logs, diffs and prompts; it never touches the command line.
  --field KEY    when the result is a JSON object, print only that field (a string prints raw).

Prints the tool's text result to stdout. Exit 0 on success, 1 when the tool or transport
fails, 2 on a usage error (bad or missing argument, unknown tool).

Transport: the same stdio MCP server Claude Code uses (the "local-llm" entry in
~/.claude.json). It is started on ai-server over ssh, and this client speaks newline-delimited
MCP JSON-RPC on the pipe. Auth is the ssh key (BatchMode, no password, no token). The server
runs on ai-server, so its own path= arguments read ai-server paths, not this machine's files.
"""

import argparse
import collections
import json
import queue
import subprocess
import sys
import threading
import time

SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=15",
       "-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4",
       "-o", "ClearAllForwardings=yes"]
HOST = "ai-server"
REMOTE = "cd /ai/mcp-servers/local-llm && /ai/venvs/local-llm-mcp/bin/python local_llm_mcp.py"
PROTOCOL = "2025-06-18"
HANDSHAKE_TIMEOUT = 120


def die(msg, code=2):
    sys.stderr.write("llm.py: %s\n" % msg)
    sys.exit(code)


class Client:
    def __init__(self):
        self.proc = subprocess.Popen(SSH + [HOST, REMOTE], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.inbox = queue.Queue()
        self.errlog = collections.deque(maxlen=40)  # server/ssh stderr, shown only on failure
        self.next_id = 0
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._err_reader, daemon=True).start()

    def _reader(self):
        for raw in self.proc.stdout:
            line = raw.strip()
            if not line:
                continue
            try:
                self.inbox.put(json.loads(line.decode("utf-8")))
            except ValueError:
                continue  # non-JSON noise on stdout is not protocol traffic
        self.inbox.put(None)

    def _err_reader(self):
        for raw in self.proc.stderr:
            self.errlog.append(raw.decode("utf-8", errors="replace").rstrip())

    def tail_err(self, n=6):
        return "\n".join(list(self.errlog)[-n:])

    def _send(self, obj):
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8") + b"\n"
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            raise RuntimeError("server pipe closed (ssh to %s failed?)" % HOST)

    def _reply_to_server_request(self, msg):
        if msg.get("method") == "ping":
            self._send({"jsonrpc": "2.0", "id": msg["id"], "result": {}})
        else:
            self._send({"jsonrpc": "2.0", "id": msg["id"],
                        "error": {"code": -32601, "message": "not supported by llm.py"}})

    def request(self, method, params, timeout):
        self.next_id += 1
        rid = self.next_id
        self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise RuntimeError("no reply to %s within %ds" % (method, timeout))
            try:
                msg = self.inbox.get(timeout=left)
            except queue.Empty:
                raise RuntimeError("no reply to %s within %ds" % (method, timeout))
            if msg is None:
                raise RuntimeError("server closed the connection")
            if "method" in msg and "id" in msg:      # server asking us something
                self._reply_to_server_request(msg)
                continue
            if "method" in msg:                      # notification (progress, logging)
                continue
            if msg.get("id") == rid:
                return msg

    def notify(self, method, params=None):
        obj = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            obj["params"] = params
        self._send(obj)

    def close(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def fail_with_stderr(c, msg):
    sys.stderr.write("llm.py: %s\n" % msg)
    tail = c.tail_err()
    if tail:
        sys.stderr.write("--- server/ssh stderr (tail) ---\n%s\n" % tail)


def open_session():
    c = Client()
    try:
        r = c.request("initialize", {"protocolVersion": PROTOCOL, "capabilities": {},
                                     "clientInfo": {"name": "llm.py", "version": "1"}},
                      HANDSHAKE_TIMEOUT)
        if "error" in r:
            raise RuntimeError("initialize failed: %s" % r["error"].get("message"))
        c.notify("notifications/initialized")
        return c
    except Exception:
        c.close()
        raise


def list_tools(c):
    r = c.request("tools/list", {}, HANDSHAKE_TIMEOUT)
    if "error" in r:
        raise RuntimeError("tools/list failed: %s" % r["error"].get("message"))
    return r["result"]["tools"]


def base_type(prop):
    t = prop.get("type")
    if isinstance(t, list):
        t = next((x for x in t if x != "null"), None)
    if t is None:
        for alt in prop.get("anyOf", []) + prop.get("oneOf", []):
            bt = base_type(alt)
            if bt and bt != "null":
                return bt
    return t


def coerce(key, raw, prop):
    t = base_type(prop)
    try:
        if t == "integer":
            return int(raw)
        if t == "number":
            return float(raw)
        if t == "boolean":
            low = raw.strip().lower()
            if low in ("1", "true", "yes", "on"):
                return True
            if low in ("0", "false", "no", "off"):
                return False
            raise ValueError("not a boolean")
        if t == "array":
            s = raw.strip()
            if s.startswith("["):
                return json.loads(s)
            return [x.strip() for x in s.split(",") if x.strip()]
        if t == "object":
            return json.loads(raw)
    except ValueError as e:
        die("argument %s: %s is not a valid %s (%s)" % (key, repr(raw[:60]), t, e))
    return raw


def split_kv(item, flag):
    if "=" not in item or item.startswith("="):
        die("%s wants K=V, got %r" % (flag, item))
    return item.split("=", 1)


def read_source(path):
    try:
        if path == "-":
            data = sys.stdin.buffer.read()
        else:
            with open(path, "rb") as f:
                data = f.read()
    except OSError as e:
        die("cannot read %s: %s" % (path, e.strerror or e))
    return data.decode("utf-8", errors="replace")


def build_args(tool, arg_items, file_items):
    schema = tool.get("inputSchema", {})
    props = schema.get("properties", {})
    required = schema.get("required", [])
    raw = {}
    for item in arg_items:
        k, v = split_kv(item, "--arg")
        raw[k] = v
    for item in file_items:
        k, path = split_kv(item, "--file")
        raw[k] = read_source(path)
    args = {}
    for k, v in raw.items():
        if k not in props:
            die("%s has no argument %r; valid: %s" % (tool["name"], k, ", ".join(sorted(props))))
        args[k] = coerce(k, v, props[k])
    missing = [k for k in required if k not in args]
    if missing:
        die("%s needs: %s (pass --arg K=V or --file K=PATH)" % (tool["name"], ", ".join(missing)))
    return args


def result_text(res):
    parts = [c.get("text", "") for c in res.get("content", []) if c.get("type") == "text"]
    text = "\n".join(parts)
    if not text and res.get("structuredContent") is not None:
        text = json.dumps(res["structuredContent"], indent=2, ensure_ascii=False)
    return text


def emit(text):
    data = text if text.endswith("\n") else text + "\n"
    sys.stdout.buffer.write(data.encode("utf-8"))
    sys.stdout.flush()


def run(a):
    try:
        c = open_session()
    except RuntimeError as e:
        sys.stderr.write("llm.py: cannot reach the local-llm server on %s: %s\n" % (HOST, e))
        return 1
    except OSError as e:
        sys.stderr.write("llm.py: cannot start ssh to %s: %s\n" % (HOST, e))
        return 1
    try:
        tools = list_tools(c)
        if a.list:
            for t in tools:
                first = (t.get("description") or "").strip().split("\n")[0]
                emit("%s\t%s" % (t["name"], first))
            return 0
        if a.schema:
            t = next((x for x in tools if x["name"] == a.schema), None)
            if t is None:
                die("no tool named %s (try --list)" % a.schema)
            emit(json.dumps(t, indent=2, ensure_ascii=False))
            return 0
        tool = next((x for x in tools if x["name"] == a.tool), None)
        if tool is None:
            die("no tool named %s (try --list)" % a.tool)
        args = build_args(tool, a.arg, a.file)
        r = c.request("tools/call", {"name": a.tool, "arguments": args}, a.timeout)
        if "error" in r:
            sys.stderr.write("llm.py: %s\n" % r["error"].get("message", r["error"]))
            return 1
        res = r["result"]
        text = result_text(res)
        if a.field:
            try:
                obj = json.loads(text)
            except ValueError:
                die("--field %s: the result is not JSON" % a.field, 1)
            if not isinstance(obj, dict) or a.field not in obj:
                die("--field %s: no such field in the result" % a.field, 1)
            val = obj[a.field]
            text = val if isinstance(val, str) else json.dumps(val, ensure_ascii=False)
        emit(text)
        return 1 if res.get("isError") else 0
    except RuntimeError as e:
        fail_with_stderr(c, str(e))
        return 1
    finally:
        c.close()


def main(argv):
    ap = argparse.ArgumentParser(description="Call the ai-server local-llm MCP tools.")
    ap.add_argument("tool", nargs="?", help="tool name, e.g. llm_complete")
    ap.add_argument("--list", action="store_true", help="list tools and exit")
    ap.add_argument("--schema", metavar="TOOL", help="show one tool's arguments and exit")
    ap.add_argument("--arg", action="append", default=[], metavar="K=V")
    ap.add_argument("--file", action="append", default=[], metavar="K=PATH")
    ap.add_argument("--field", metavar="KEY", help="print only this field of a JSON result")
    ap.add_argument("--timeout", type=float, default=900, help="seconds for the tool call")
    a = ap.parse_args(argv)
    if not (a.list or a.schema or a.tool):
        ap.print_usage(sys.stderr)
        return 2
    return run(a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
