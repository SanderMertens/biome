#!/usr/bin/env bash
#
# Render every scene in etc/scenes to test/out/<scene>.ppm and compare
# against the baseline images in test/scenes/<scene>.ppm.
#
# Generate the baselines first with: ./test_baseline.sh
#
# Usage: ./test_scenes.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build/release"
ENGINE="./$BUILD_DIR/flecs"
SCENES_DIR="etc/scenes"
BASELINE_DIR="test/scenes"
OUT_DIR="test/out"

echo "==> Building engine (Release)"
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD_DIR" --parallel 8

if [[ ! -x "$ENGINE" ]]; then
  echo "error: $ENGINE not found or not executable after build." >&2
  exit 1
fi

if [[ ! -d "$BASELINE_DIR" ]]; then
  echo "error: baseline directory $BASELINE_DIR not found. Run ./test_baseline.sh first." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
rm -rf test/out/*

shopt -s nullglob
scenes=("$SCENES_DIR"/*.flecs)
shopt -u nullglob

if [[ ${#scenes[@]} -eq 0 ]]; then
  echo "error: no scenes found in $SCENES_DIR" >&2
  exit 1
fi

render_failures=()
missing_baselines=()
mismatches=()
passed=0
results=""

for scene in "${scenes[@]}"; do
  name="$(basename "$scene" .flecs)"
  out="$OUT_DIR/$name.ppm"
  baseline="$BASELINE_DIR/$name.ppm"
  pct="0.00"

  echo "==> $name"

  if ! "$ENGINE" --scene "$scene" --frame-out "$out"; then
    echo "    RENDER FAILED" >&2
    render_failures+=("$name")
    results+="${name}"$'\t'"render_fail"$'\t'"${pct}"$'\n'
    continue
  fi

  if [[ ! -f "$baseline" ]]; then
    echo "    no baseline at $baseline" >&2
    missing_baselines+=("$name")
    results+="${name}"$'\t'"missing_baseline"$'\t'"${pct}"$'\n'
    continue
  fi

  diff_bytes=$({ cmp -l "$baseline" "$out" 2>/dev/null || true; } | wc -l | tr -d ' ')
  total_bytes=$(wc -c < "$baseline" | tr -d ' ')
  pct=$(awk "BEGIN { printf \"%.2f\", ($diff_bytes/$total_bytes)*100 }")
  pass_threshold=$(awk "BEGIN { print (($diff_bytes/$total_bytes)*100 < 0.1) ? 1 : 0 }")

  if [[ "$pass_threshold" -eq 1 ]]; then
    echo "    ok (${pct}% diff)"
    passed=$((passed + 1))
    results+="${name}"$'\t'"ok"$'\t'"${pct}"$'\n'
  else
    diff_out="$OUT_DIR/${name}_diff.ppm"
    python3 - "$baseline" "$out" "$diff_out" <<'PY'
import sys

def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    i = 0
    def skip_ws():
        nonlocal i
        while i < len(data):
            c = data[i:i+1]
            if c in (b' ', b'\t', b'\n', b'\r'):
                i += 1
            elif c == b'#':
                while i < len(data) and data[i:i+1] != b'\n':
                    i += 1
            else:
                break
    def read_token():
        nonlocal i
        skip_ws()
        start = i
        while i < len(data) and data[i:i+1] not in (b' ', b'\t', b'\n', b'\r'):
            i += 1
        return data[start:i]
    magic = read_token()
    if magic != b'P6':
        sys.stderr.write(f"unsupported PPM magic: {magic!r}\n")
        sys.exit(1)
    w = int(read_token())
    h = int(read_token())
    mv = int(read_token())
    i += 1  # single whitespace byte after maxval
    return w, h, mv, data[i:]

wa, ha, _, pa = read_ppm(sys.argv[1])
wb, hb, _, pb = read_ppm(sys.argv[2])

if (wa, ha) != (wb, hb):
    sys.stderr.write(f"dimensions differ: {wa}x{ha} vs {wb}x{hb}\n")
    sys.exit(1)

n = (min(len(pa), len(pb)) // 3) * 3
out = bytearray(n)
# For each pixel:
#   current brighter than baseline -> bright green
#   current darker  than baseline -> bright red
#   same color-shift, same luminance -> yellow (rare)
#   identical -> dimmed grayscale of baseline for context
for i in range(0, n, 3):
    ar, ag, ab = pa[i], pa[i+1], pa[i+2]
    br, bg, bb = pb[i], pb[i+1], pb[i+2]
    if ar == br and ag == bg and ab == bb:
        g = (ar * 30 + ag * 59 + ab * 11) // 100 // 4  # ~25% luminance
        out[i] = g
        out[i+1] = g
        out[i+2] = g
    else:
        la = ar * 30 + ag * 59 + ab * 11
        lb = br * 30 + bg * 59 + bb * 11
        if lb > la:
            out[i] = 0
            out[i+1] = 255
            out[i+2] = 0
        elif lb < la:
            out[i] = 255
            out[i+1] = 0
            out[i+2] = 0
        else:
            out[i] = 255
            out[i+1] = 255
            out[i+2] = 0

with open(sys.argv[3], 'wb') as f:
    f.write(f"P6\n{wa} {ha}\n255\n".encode())
    f.write(bytes(out))
PY
    echo "    MISMATCH ${pct}% diff (see $out vs $baseline, diff: $diff_out)" >&2
    mismatches+=("$name (${pct}%)")
    results+="${name}"$'\t'"mismatch"$'\t'"${pct}"$'\n'
  fi
done

echo
echo "Passed:           $passed / ${#scenes[@]}"
echo "Render failures:  ${#render_failures[@]}"
echo "Missing baseline: ${#missing_baselines[@]}"
echo "Mismatches:       ${#mismatches[@]}"

echo
echo "Generating HTML report..."
RESULTS="$results" python3 - "$OUT_DIR" "$BASELINE_DIR" <<'PY'
import sys, os, zlib, struct, binascii

out_dir = sys.argv[1]
baseline_dir = sys.argv[2]
results_env = os.environ.get('RESULTS', '')

def read_ppm(path):
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        data = f.read()
    i = 0
    def skip_ws():
        nonlocal i
        while i < len(data):
            c = data[i:i+1]
            if c in (b' ', b'\t', b'\n', b'\r'):
                i += 1
            elif c == b'#':
                while i < len(data) and data[i:i+1] != b'\n':
                    i += 1
            else:
                break
    def read_token():
        nonlocal i
        skip_ws()
        start = i
        while i < len(data) and data[i:i+1] not in (b' ', b'\t', b'\n', b'\r'):
            i += 1
        return data[start:i]
    magic = read_token()
    if magic != b'P6':
        return None
    w = int(read_token())
    h = int(read_token())
    _ = int(read_token())
    i += 1
    return w, h, data[i:i + w * h * 3]

def write_png(path, w, h, rgb):
    def chunk(tag, data):
        crc = binascii.crc32(tag + data) & 0xffffffff
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', crc)
    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)
    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(rgb[y * stride:(y + 1) * stride])
    idat = zlib.compress(bytes(raw), 6)
    with open(path, 'wb') as f:
        f.write(sig)
        f.write(chunk(b'IHDR', ihdr))
        f.write(chunk(b'IDAT', idat))
        f.write(chunk(b'IEND', b''))

def to_png(src, dst):
    ppm = read_ppm(src)
    if ppm is None:
        return False
    w, h, rgb = ppm
    if len(rgb) < w * h * 3:
        return False
    write_png(dst, w, h, rgb)
    return True

status_class = {
    'ok': 'ok',
    'mismatch': 'mismatch',
    'render_fail': 'fail',
    'missing_baseline': 'fail',
}
status_text = {
    'ok': 'ok',
    'mismatch': 'mismatch',
    'render_fail': 'render failed',
    'missing_baseline': 'no baseline',
}

rows = []
for line in results_env.splitlines():
    if not line:
        continue
    parts = line.split('\t')
    if len(parts) < 3:
        continue
    name, status, pct = parts[0], parts[1], parts[2]

    baseline_src = os.path.join(baseline_dir, name + '.ppm')
    current_src = os.path.join(out_dir, name + '.ppm')
    diff_src = os.path.join(out_dir, name + '_diff.ppm')

    baseline_png = name + '_baseline.png'
    current_png = name + '_current.png'
    diff_png = name + '_diff.png'

    has_b = to_png(baseline_src, os.path.join(out_dir, baseline_png))
    has_c = to_png(current_src, os.path.join(out_dir, current_png))
    has_d = to_png(diff_src, os.path.join(out_dir, diff_png))

    rows.append((
        name, status, pct,
        baseline_png if has_b else None,
        current_png if has_c else None,
        diff_png if has_d else None,
    ))

html = [
    '<!DOCTYPE html>',
    '<html><head><meta charset="utf-8"><title>flecs-engine scene tests</title>',
    '<style>',
    'body { font-family: -apple-system, sans-serif; background: #1a1a1a; color: #eee; margin: 20px; }',
    'h1 { font-weight: 400; }',
    'table { border-collapse: collapse; width: 100%; }',
    'th, td { padding: 10px; border-bottom: 1px solid #333; vertical-align: top; }',
    'th { text-align: left; background: #222; font-weight: 600; }',
    'img { max-width: 480px; display: block; background: #000; }',
    'a { display: inline-block; }',
    'a img:hover { cursor: pointer; }',
    '.name { font-weight: 600; min-width: 180px; }',
    '.ok { color: #8f8; }',
    '.mismatch { color: #f88; }',
    '.fail { color: #fa0; }',
    '.pct { color: #ccc; font-size: 0.9em; }',
    '.placeholder { color: #555; font-style: italic; }',
    '</style></head><body>',
    f'<h1>Scene test results &mdash; {len(rows)} scenes</h1>',
    '<table>',
    '<tr><th>Scene</th><th>Baseline</th><th>Current</th><th>Diff</th></tr>',
]

for name, status, pct, b, c, d in rows:
    cls = status_class.get(status, 'fail')
    txt = status_text.get(status, status)
    cell_b = f'<a href="{b}" target="_blank"><img src="{b}" loading="lazy"></a>' if b else '<span class="placeholder">&mdash;</span>'
    cell_c = f'<a href="{c}" target="_blank"><img src="{c}" loading="lazy"></a>' if c else '<span class="placeholder">&mdash;</span>'
    cell_d = f'<a href="{d}" target="_blank"><img src="{d}" loading="lazy"></a>' if d else '<span class="placeholder">&mdash;</span>'
    html.append('<tr>')
    html.append(f'<td class="name">{name}<br><span class="{cls}">{txt}</span> <span class="pct">{pct}%</span></td>')
    html.append(f'<td>{cell_b}</td>')
    html.append(f'<td>{cell_c}</td>')
    html.append(f'<td>{cell_d}</td>')
    html.append('</tr>')

html.append('</table></body></html>')

index_path = os.path.join(out_dir, 'index.html')
with open(index_path, 'w') as f:
    f.write('\n'.join(html))

print(f"  wrote {index_path}")
PY

fail_total=$(( ${#render_failures[@]} + ${#missing_baselines[@]} + ${#mismatches[@]} ))
if [[ $fail_total -gt 0 ]]; then
  echo
  if [[ ${#render_failures[@]} -gt 0 ]]; then
    echo "Render failures:" >&2
    for f in "${render_failures[@]}"; do echo "  - $f" >&2; done
  fi
  if [[ ${#missing_baselines[@]} -gt 0 ]]; then
    echo "Missing baselines:" >&2
    for f in "${missing_baselines[@]}"; do echo "  - $f" >&2; done
  fi
  if [[ ${#mismatches[@]} -gt 0 ]]; then
    echo "Mismatches:" >&2
    for f in "${mismatches[@]}"; do echo "  - $f" >&2; done
  fi
  exit 1
fi

echo "All scenes match their baseline."
