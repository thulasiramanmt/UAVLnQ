#!/usr/bin/env bash
# Team addition (task 1.4.2): runs a full UAVLnQ mission inside the container - three drones, the parser and the controller - in a fresh run folder, with no display needed.
#
# Usage (from the team repo):
#   docker compose -f docker-compose.yml -f docker-compose.uavlnq.yml run --rm uavlnq
#
# Optional settings (environment variables):
#   UAVLNQ_RUN_DIR  where this run's files go   (default /data/uavlnq/run-<date>_<time>)
#   QGC_HOST        computer running QGroundControl (default host.docker.internal, i.e. your laptop)
#   UAVLNQ_HOME     drones' start point          (default CMAC, the reference point UAVLnQ's ns-3 code uses)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY="${UAVLNQ_PYTHON:-/opt/uavlnq-venv/bin/python}"
NS3_BIN="${UAVLNQ_NS3_BIN:?UAVLNQ_NS3_BIN is not set - run this inside the boeing3-uavlnq image}"
HOME_LOC="${UAVLNQ_HOME:--35.363261,149.165230,584,353}"
QGC_HOST="${QGC_HOST:-host.docker.internal}"
RUN_DIR="${UAVLNQ_RUN_DIR:-/data/uavlnq/run-$(date +%Y%m%d_%H%M%S)}"
DRONES=3
ARDUPILOT_HOME="${ARDUPILOT_HOME:-/opt/ardupilot}"
COPTER_PARM="$ARDUPILOT_HOME/Tools/autotest/default_params/copter.parm"
ARDUCOPTER="$(command -v arducopter || echo "$ARDUPILOT_HOME/build/sitl/bin/arducopter")"
[[ -x "$ARDUCOPTER" ]] || { echo "ERROR: arducopter not found in the image" >&2; exit 1; }
[[ -f "$COPTER_PARM" ]] || { echo "ERROR: $COPTER_PARM not found in the image" >&2; exit 1; }

# The controller must start ns-3 without xterm (task 1.5.6); a container has no display.
if grep -q '"xterm"' "$REPO/connect.py"; then
    echo "ERROR: connect.py still opens ns-3 in xterm. Commit task 1.5.6 first." >&2
    exit 1
fi

PIDS=()
cleanup() {
    for pid in "${PIDS[@]}"; do kill "$pid" 2>/dev/null || true; done
    wait 2>/dev/null || true
    echo "Stopped. This run's files are in $RUN_DIR"
}
trap cleanup EXIT

wait_for() {  # file, text to wait for, seconds, label
    local file=$1 text=$2 limit=$3 label=$4 waited
    for ((waited = 0; waited < limit; waited++)); do
        if grep -q "$text" "$file" 2>/dev/null; then
            echo "  $label: ready"
            return 0
        fi
        sleep 1
    done
    echo "  WARNING: $label not ready after ${limit}s - see $file" >&2
    return 1
}

# A fresh folder per run: old orders can never be replayed, and each run keeps its own evidence.
mkdir -p "$RUN_DIR"/mission "$RUN_DIR"/logs "$RUN_DIR"/ns3-output "$RUN_DIR"/sitl
cd "$RUN_DIR"
echo "Run folder: $RUN_DIR"

# Same settings as the repo's drones_config.json, with this container's ns-3 path and output folder.
"$PY" - "$REPO/drones_config.json" "$NS3_BIN" "$RUN_DIR/ns3-output" > drones_config.json <<'PYEOF'
import json, shlex, sys
cfg = json.load(open(sys.argv[1]))
ns3 = cfg["NS3_config"]
ns3["ns3_bin"] = sys.argv[2]
params = [p for p in shlex.split(ns3["parameters"]) if not p.startswith("--o=")]
ns3["parameters"] = " ".join(["--o=" + sys.argv[3]] + params)
print(json.dumps(cfg, indent=2))
PYEOF

# The leader keeps its own route; the followers start with no orders.
cp "$REPO/mission/mission-drone-1.pln" mission/
for ((n = 2; n <= DRONES; n++)); do : > "mission/mission-drone-$n.pln"; done

# Send a copy of every drone's data to QGroundControl on the laptop, if that computer can be reached.
QGC_OUT=()
if getent hosts "$QGC_HOST" > /dev/null; then
    QGC_OUT=("--out=udp:$QGC_HOST:14550")
    echo "QGroundControl: drone data is also sent to $QGC_HOST:14550"
else
    echo "QGroundControl: $QGC_HOST not reachable, skipping"
fi

echo "Starting $DRONES drones..."
for ((i = 0; i < DRONES; i++)); do
    n=$((i + 1))
    dir="sitl/drone$n"
    mkdir -p "$dir"
    (cd "$dir" && exec "$ARDUCOPTER" --model + --speedup 1 -I"$i" --sysid "$n" --home "$HOME_LOC" \
        --defaults "$COPTER_PARM") > "$dir/arducopter.log" 2>&1 &
    PIDS+=($!)
done
sleep 3

# One MAVProxy per drone, with the same ports as the Codespace setup: 1455x, 1456x and 1457x.
for ((i = 0; i < DRONES; i++)); do
    n=$((i + 1))
    dir="sitl/drone$n"
    base=$((14551 + 10 * i))
    mavproxy.py --master="tcp:127.0.0.1:$((5760 + 10 * i))" \
        --out="udp:127.0.0.1:$base" --out="udp:127.0.0.1:$((base + 1))" --out="udp:127.0.0.1:$((base + 2))" \
        "${QGC_OUT[@]}" --non-interactive --state-basedir="$dir" > "$dir/mavproxy.log" 2>&1 &
    PIDS+=($!)
done

echo "Waiting for each drone's GPS..."
for ((n = 1; n <= DRONES; n++)); do
    wait_for "sitl/drone$n/mavproxy.log" "using GPS" 240 "drone $n" || true
done

echo "Starting the parser..."
"$PY" -u "$REPO/Mavlink-NS3-Parser.py" > logs/parser.log 2>&1 &
PIDS+=($!)
for ((n = 1; n <= DRONES; n++)); do
    wait_for logs/parser.log "Connected to Drone $n" 60 "parser to drone $n" || true
done

echo "Starting the controller. The run ends by itself after every drone lands."
"$PY" -u "$REPO/connect.py" 2>&1 | tee logs/connect.log

echo
echo "Leader's orders received by the followers:"
grep "Appended Mission Item" logs/parser.log || echo "  none"
