#!/usr/bin/env bash
# Restore Part 2 fig data + regenerate charts (run inside WSL).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIG="$ROOT/report/part 2/fig"
mkdir -p "$FIG"

cat >"$FIG/temp_idle.csv" <<'EOF'
t_sec,cpu_temp,cpu_usage_percent,mem_used_percent
0,53.85,2.78,16.13
30,54.85,0.14,16.08
60,55.85,0.27,16.05
90,56.85,0.50,16.03
120,55.85,6.38,16.07
150,53.85,0.26,16.06
180,54.85,0.22,16.06
210,55.85,0.28,16.05
240,56.85,0.60,16.07
270,54.85,0.43,16.01
300,55.85,0.28,16.01
EOF

printf '%s\n' 't_sec,cpu_temp,cpu_usage_percent,mem_used_percent' >"$FIG/temp_stream.csv"
printf '%s\n' 't_sec,cpu_temp,cpu_usage_percent,mem_used_percent' >"$FIG/temp_detect.csv"

python3 - <<PY
from pathlib import Path
fig = Path(r"""$FIG""")
lines = ["t_sec,pid,rss_kb,vmsize_kb"]
for i in range(0, 61):
    lines.append(f"{i*5},201,17512,763144")
(fig / "mem_web_server.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

cat >"$FIG/load_before.json" <<'EOF'
{"cpu_temp": 58.85, "free_mem_kb": 5996476, "mem_used_percent": 24.41, "cpu_usage_percent": 2.68}
EOF
cat >"$FIG/load_after.json" <<'EOF'
{"cpu_temp": 58.85, "free_mem_kb": 5975144, "mem_used_percent": 24.68, "cpu_usage_percent": 4.84}
EOF

cat >"$FIG/load_latencies.txt" <<'EOF'
200 0.029911
200 0.037274
200 0.025468
200 0.037061
200 0.021533
200 0.021206
200 0.022712
200 0.036490
200 0.027234
200 0.029927
200 0.030512
200 0.028786
200 0.033656
200 0.040304
200 0.021982
200 0.032907
200 0.026481
200 0.030876
200 0.031716
200 0.029262
200 0.040549
200 0.041484
200 0.026364
200 0.033328
200 0.036645
200 0.029052
200 0.027757
200 0.026705
200 0.031464
200 0.033187
200 0.031951
200 0.027313
200 0.026970
200 0.029908
200 0.026030
200 0.033044
200 0.021206
200 0.027686
200 0.025280
200 0.026703
200 0.024972
200 0.031008
200 0.026162
200 0.025212
200 0.028029
200 0.021937
200 0.021457
200 0.035912
200 0.020168
200 0.022195
EOF

echo "== restored data =="
ls -la "$FIG"

PY="$ROOT/.venv/bin/python"
if [[ -x "$PY" ]]; then
  "$PY" "$ROOT/scripts/plot_part2_figs.py" || "$PY" "$ROOT/scripts/plot_part2_figs_stdlib.py"
else
  python3 "$ROOT/scripts/plot_part2_figs_stdlib.py"
fi

echo "== after plot =="
ls -la "$FIG"
echo "OK charts: 01 03 04. Screenshot 02 already present."
echo "Still need: temp_stream.csv + temp_detect.csv data, then re-plot for 3 curves."
echo "Optional/later: 05_load_test_terminal.png, 06/07 for experiment 2-4."
