#!/usr/bin/env bash
# Smoke-test pcg-server: health + cook a sample .pcg; optionally compare to in-process native.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PCG_SERVER_PORT:-17890}"
BASE="http://127.0.0.1:${PORT}"
GRAPH="${1:-$ROOT/examples/graphs/boolean-subtract-box.pcg}"
export COMPARE_NATIVE="${COMPARE_NATIVE:-1}"

echo "==> GET $BASE/v1/health"
HEALTH="$(curl -sS "$BASE/v1/health")"
echo "$HEALTH"
echo "$HEALTH" | grep -q '"ok":true'

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
META='{"seed":42,"api_version":1}'
printf '%s' "$META" >"$TMP/meta.json"
cp "$GRAPH" "$TMP/graph.json"

echo "==> POST $BASE/v1/cook ($GRAPH)"
curl -sS -X POST "$BASE/v1/cook" \
  -F "meta=<$TMP/meta.json;type=application/json" \
  -F "graph=<$TMP/graph.json;type=application/json" \
  -o "$TMP/out.bin" \
  -D "$TMP/hdr.txt"

python3 - <<PY
import hashlib, os, struct, ctypes
from ctypes import c_char_p, c_int, c_uint, byref

data = open("$TMP/out.bin", "rb").read()
assert len(data) >= 56, f"short response {len(data)}"
magic, ver, code, kind = struct.unpack_from("<IIiI", data, 0)
assert magic == 0x52474350, hex(magic)
assert ver == 1, ver
print(f"HTTP result code={code} kind={kind} bytes={len(data)}")
assert code == 0, f"cook failed code={code}"

off = 56
def blob():
    global off
    (n,) = struct.unpack_from("<I", data, off); off += 4
    b = data[off:off+n]; off += n
    return b

err = blob(); json_b = blob(); mesh = blob(); points = blob()
assert not err, err

if kind == 2:
    assert len(mesh) >= 16 and struct.unpack_from("<I", mesh, 0)[0] == 0x4D474350
    vc, ic = struct.unpack_from("<ii", mesh, 8)
    print(f"mesh verts={vc} indices={ic} mesh_bytes={len(mesh)}")
    payload = mesh
elif kind == 3:
    assert len(points) >= 16 and struct.unpack_from("<I", points, 0)[0] == 0x50544750
    pc = struct.unpack_from("<I", points, 8)[0]
    print(f"points count={pc} points_bytes={len(points)}")
    payload = points
elif kind == 1:
    print(f"json bytes={len(json_b)}")
    payload = json_b
else:
    raise SystemExit(f"unexpected kind {kind}")

http_hash = hashlib.sha256(payload).hexdigest()
print(f"HTTP payload sha256={http_hash}")

if os.environ.get("COMPARE_NATIVE", "1") != "1":
    print("OK (skip native compare)")
    raise SystemExit(0)

root = "$ROOT"
dylib_candidates = [
    f"{root}/pcg-server/build/pcg-core/libPcgCore.dylib",
    f"{root}/pcg-core/build/libPcgCore.dylib",
]
dylib = next((p for p in dylib_candidates if os.path.isfile(p)), None)
if not dylib:
    print("WARN: no PcgCore dylib for native compare; HTTP path OK")
    print("OK")
    raise SystemExit(0)

lib = ctypes.CDLL(dylib)
lib.pcg_execute_graph_v10.restype = c_int

graph = open("$TMP/graph.json", "rb").read()
out_json = (ctypes.c_char * (8 * 1024 * 1024))()
out_mesh = (ctypes.c_char * (32 * 1024 * 1024))()
out_points = (ctypes.c_char * (16 * 1024 * 1024))()
out_perf = (ctypes.c_char * (64 * 1024))()
out_geo = (ctypes.c_char * (16 * 1024 * 1024))()
out_hf = (ctypes.c_char * (8 * 1024 * 1024))()
errbuf = (ctypes.c_char * 1024)()
kind_n = c_int(0)
pc_n = c_int(0)
paf = c_uint(0)
vc = c_int(0)
ic = c_int(0)
geo_w = c_int(0)
hf_w = c_int(0)

class CookStats(ctypes.Structure):
    _fields_ = [
        ("nodes_executed", c_int),
        ("nodes_skipped", c_int),
        ("graph_execute_ms", ctypes.c_double),
        ("binary_write_ms", ctypes.c_double),
    ]
stats = CookStats()

rc = lib.pcg_execute_graph_v10(
    c_char_p(graph),
    c_int(42),
    None, c_int(0),
    None, c_int(0),
    None, c_int(0),
    None, c_int(0),
    byref(kind_n),
    out_json, c_int(len(out_json)),
    out_mesh, c_int(len(out_mesh)),
    out_points, c_int(len(out_points)),
    byref(pc_n), byref(paf),
    byref(vc), byref(ic),
    byref(stats),
    out_perf, c_int(len(out_perf)),
    out_geo, c_int(len(out_geo)), byref(geo_w),
    out_hf, c_int(len(out_hf)), byref(hf_w),
    errbuf, c_int(1024),
)
assert rc == 0, errbuf.value.decode("utf-8", "replace")
assert kind_n.value == kind, (kind_n.value, kind)

def mesh_size(buf, vertex_count, index_count):
    raw = bytes(memoryview(buf)[:24])
    m = struct.unpack_from("<I", raw, 0)[0]
    if m == 0x534D4350:
        count = struct.unpack_from("<i", raw, 8)[0]
        header = 12 + count * 8
        total = header
        size_off = 12 + count * 4
        full = bytes(memoryview(buf)[:header])
        for i in range(count):
            total += struct.unpack_from("<i", full, size_off + i * 4)[0]
        return total
    header = 16
    normal = color = uv = mat = 0
    version = struct.unpack_from("<I", raw, 4)[0]
    if version in (2, 3):
        header = 24 if version == 3 else 20
        flags = struct.unpack_from("<I", raw, 16)[0]
        if flags & 1: normal = vertex_count * 12
        if flags & 2: color = vertex_count * 16
        if flags & 4: uv = vertex_count * 8
        if version == 3:
            mat = struct.unpack_from("<i", raw, 20)[0]
    return header + vertex_count * 12 + index_count * 4 + normal + color + uv + mat

if kind == 2:
    nsz = mesh_size(out_mesh, vc.value, ic.value)
    native_payload = bytes(memoryview(out_mesh)[:nsz])
elif kind == 3:
    native_payload = bytes(memoryview(out_points)[:len(points)])
else:
    native_payload = bytes(memoryview(out_json)[:len(json_b)])

native_hash = hashlib.sha256(native_payload).hexdigest()
print(f"Native payload sha256={native_hash} (dylib={dylib})")
assert native_payload == payload, "HTTP vs native payload mismatch"
print("OK (HTTP == native)")
PY
