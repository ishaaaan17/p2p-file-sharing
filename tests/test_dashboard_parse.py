"""Tests dashboard.py's parse_telemetry_frame without importing Streamlit:
extracts just that function from dashboard.py and exercises it with the
exact wire format the C++ engine produces (9-byte Packet header + JSON),
plus raw JSON. Run: python tests/test_dashboard_parse.py"""
import ast
import json
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
src = (ROOT / "dashboard.py").read_text()
tree = ast.parse(src)
fn = next(n for n in tree.body
          if isinstance(n, ast.FunctionDef) and n.name == "parse_telemetry_frame")
ns = {"json": json, "PACKET_HEADER_SIZE": 9}
exec(compile(ast.Module(body=[fn], type_ignores=[]), "dashboard_fn", "exec"), ns)
parse = ns["parse_telemetry_frame"]

payload = json.dumps({"type": "PIECE_UPDATE", "index": 2, "status": "Completed"}).encode()
expected = {"type": "PIECE_UPDATE", "index": 2, "status": "Completed"}

# Raw JSON (old format) still parses.
assert parse(payload) == expected, "raw JSON parse failed"

# Packet-framed JSON (what the C++ engine actually sends) parses too.
framed = bytes([3]) + struct.pack(">IHH", 0, len(payload), 0) + payload
assert parse(framed) == expected, "Packet-framed JSON parse failed"

print("test_dashboard_parse: PASS (raw JSON + Packet-framed JSON)")
sys.exit(0)
