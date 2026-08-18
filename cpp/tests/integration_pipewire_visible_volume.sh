#!/usr/bin/env bash
set -euo pipefail

# Manual integration regression for PipeWire + pipewire-pulse + WirePlumber.
# It creates a completely separate runtime/config/state tree and a private
# D-Bus session. No socket, stream, or keyboard-volume-app configuration from
# the logged-in desktop session is visible to the test processes.

if [[ ${KV_PIPEWIRE_TEST_IN_DBUS:-0} != 1 ]]; then
    exec dbus-run-session -- env KV_PIPEWIRE_TEST_IN_DBUS=1 "$0" "$@"
fi

BUILD_DIR=${1:-}
if [[ -z $BUILD_DIR ]]; then
    echo "usage: $0 /absolute/path/to/cmake-build-dir" >&2
    exit 2
fi
BUILD_DIR=$(realpath "$BUILD_DIR")
APP="$BUILD_DIR/keyboard-volume-app"
KVCTL="$BUILD_DIR/kv-ctl"
PW_HELPER="$BUILD_DIR/tests/pw_volume_test_helper"
if [[ ! -x $APP || ! -x $KVCTL || ! -x $PW_HELPER ]]; then
    echo "app, kv-ctl and pw_volume_test_helper must be built in $BUILD_DIR" >&2
    exit 2
fi

for tool in pipewire pipewire-pulse wireplumber pactl pacat pw-cli wpctl python3 dbus-send; do
    command -v "$tool" >/dev/null || {
        echo "missing integration dependency: $tool" >&2
        exit 2
    }
done

TEST_ROOT=$(mktemp -d /tmp/keyboard-volume-app-pw-test-XXXXXX)
mkdir -m 700 "$TEST_ROOT/runtime" "$TEST_ROOT/config" "$TEST_ROOT/state" "$TEST_ROOT/cache"

export XDG_RUNTIME_DIR="$TEST_ROOT/runtime"
export PIPEWIRE_RUNTIME_DIR="$TEST_ROOT/runtime"
export XDG_CONFIG_HOME="$TEST_ROOT/config"
export XDG_STATE_HOME="$TEST_ROOT/state"
export XDG_CACHE_HOME="$TEST_ROOT/cache"
export PULSE_SERVER="unix:$TEST_ROOT/runtime/pulse/native"
export QT_QPA_PLATFORM=offscreen
export QT_FORCE_STDERR_LOGGING=1
export XDG_SESSION_TYPE=x11

declare -A STREAM_PIDS=()
APP_PID=
PIPEWIRE_PID=
WIREPLUMBER_PID=
PULSE_PID=

cleanup()
{
    set +e
    for pid in "${STREAM_PIDS[@]}"; do
        kill "$pid" 2>/dev/null
    done
    [[ -n $APP_PID ]] && kill "$APP_PID" 2>/dev/null
    [[ -n $PULSE_PID ]] && kill "$PULSE_PID" 2>/dev/null
    [[ -n $WIREPLUMBER_PID ]] && kill "$WIREPLUMBER_PID" 2>/dev/null
    [[ -n $PIPEWIRE_PID ]] && kill "$PIPEWIRE_PID" 2>/dev/null
    wait 2>/dev/null
    if [[ ${KV_KEEP_PIPEWIRE_TEST_ROOT:-0} == 1 ]]; then
        echo "kept isolated test root: $TEST_ROOT"
    else
        rm -rf -- "$TEST_ROOT"
    fi
}
trap cleanup EXIT

mkdir -p "$XDG_CONFIG_HOME/keyboard-volume-app"
python3 - "$XDG_CONFIG_HOME/keyboard-volume-app/config.json" <<'PY'
import json
import sys

def profile(profile_id, app, *, duck=False):
    return {
        "id": profile_id,
        "name": profile_id,
        "app": app,
        "apps": [app],
        "app_regex": "",
        "modifiers": [],
        "hotkeys": {"volume_up": 0, "volume_down": 0, "mute": 0, "show": 0},
        "ducking": {"enabled": duck, "volume": 20, "hotkey": 0},
        "auto_switch": False,
        "vol_min": 0,
        "vol_max": 100,
        "sink": "",
    }

data = {
    "input_device": "/dev/input/keyboard-volume-app-integration-missing",
    "selected_app": "kv-active",
    "language": "en",
    "volume_step": 5,
    "profiles": [
        profile("default", "kv-active"),
        profile("inactive", "kv-inactive"),
        profile("pending", "kv-pending"),
        profile("keep", "kv-keep", duck=True),
        profile("duck-target", "kv-duck"),
    ],
    "scenes": [
        {
            "id": "visible-scene",
            "name": "visible-scene",
            "targets": [{"match": "kv-active", "volume": 42, "muted": False}],
        }
    ],
}
with open(sys.argv[1], "w", encoding="utf-8") as handle:
    json.dump(data, handle)
PY

pipewire >"$TEST_ROOT/pipewire.log" 2>&1 &
PIPEWIRE_PID=$!
wireplumber >"$TEST_ROOT/wireplumber.log" 2>&1 &
WIREPLUMBER_PID=$!
pipewire-pulse >"$TEST_ROOT/pipewire-pulse.log" 2>&1 &
PULSE_PID=$!

for _ in $(seq 1 120); do
    if [[ -S $XDG_RUNTIME_DIR/pulse/native ]] && pactl info >/dev/null 2>&1; then
        break
    fi
    sleep 0.05
done
pactl info >/dev/null

ensure_test_sinks()
{
    local sink
    for sink in kv_sink_a kv_sink_b; do
        if ! pactl -f json list sinks | python3 -c '
import json
import sys
expected = sys.argv[1]
raise SystemExit(0 if any(item.get("name") == expected for item in json.load(sys.stdin)) else 1)
' "$sink"
        then
            pactl load-module module-null-sink sink_name="$sink" >/dev/null
        fi
    done
}

ensure_test_sinks

start_stream()
{
    local app=$1
    local sink=${2:-kv_sink_a}
    pacat --playback --client-name="$app" --property="application.name=$app" \
        --property="application.process.binary=$app" --device="$sink" /dev/zero \
        >"$TEST_ROOT/$app.pacat.log" 2>&1 &
    STREAM_PIDS["$app"]=$!
}

stop_stream()
{
    local app=$1
    local pid=${STREAM_PIDS[$app]:-}
    if [[ -n $pid ]]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        unset 'STREAM_PIDS['"$app"']'
    fi
}

node_for_app()
{
    local app=$1
    pactl -f json list sink-inputs | python3 -c '
import json
import sys
app = sys.argv[1]
items = json.load(sys.stdin)
print(next((item.get("properties", {}).get("object.id", "")
            for item in items
            if item.get("properties", {}).get("application.name") == app), ""))
' "$app"
}

wait_for_node()
{
    local app=$1
    local node=
    for _ in $(seq 1 120); do
        node=$(node_for_app "$app")
        if [[ -n $node ]]; then
            printf '%s\n' "$node"
            return 0
        fi
        sleep 0.05
    done
    echo "timed out waiting for PipeWire node: $app" >&2
    return 1
}

wait_for_no_node()
{
    local app=$1
    for _ in $(seq 1 120); do
        [[ -z $(node_for_app "$app") ]] && return 0
        sleep 0.05
    done
    echo "timed out waiting for PipeWire node removal: $app" >&2
    return 1
}

assert_node_state()
{
    local label=$1
    local app=$2
    local expected_raw=$3
    local expected_channels=$4
    local node
    node=$(wait_for_node "$app")

    for _ in $(seq 1 120); do
        if python3 - "$label" "$node" "$expected_raw" "$expected_channels" <<'PY'
import json
import math
import re
import subprocess
import sys

label, node, expected_raw, expected_channels = sys.argv[1:]
expected_raw = float(expected_raw)
expected_channels = [float(value) for value in expected_channels.split(",")]
expected_linear_channels = [value ** 3 for value in expected_channels]

props = subprocess.check_output(
    ["pw-cli", "enum-params", node, "Props"], text=True, stderr=subprocess.STDOUT
)
raw_match = re.search(r"Props:volume[^\n]*\n\s+Float ([0-9.]+)", props)
channels_match = re.search(
    r"Props:channelVolumes[^\n]*\n(.*?)(?=\n\s+Prop:|\Z)", props, re.S
)
if not raw_match or not channels_match:
    raise SystemExit(1)
raw = float(raw_match.group(1))
channels = [float(value) for value in re.findall(r"Float ([0-9.]+)", channels_match.group(1))]
if len(channels) != len(expected_channels):
    raise SystemExit(1)

wpctl_text = subprocess.check_output(["wpctl", "get-volume", node], text=True)
wpctl_match = re.search(r"Volume:\s+([0-9.]+)", wpctl_text)
if not wpctl_match:
    raise SystemExit(1)
wpctl_volume = float(wpctl_match.group(1))

items = json.loads(subprocess.check_output(["pactl", "-f", "json", "list", "sink-inputs"]))
item = next(
    (value for value in items if value.get("properties", {}).get("object.id") == node),
    None,
)
if item is None:
    raise SystemExit(1)
pulse_channels = [
    channel["value"] / 65536.0 for channel in item.get("volume", {}).values()
]

tol = 0.012
if not math.isclose(raw, expected_raw, abs_tol=tol):
    raise SystemExit(1)
if any(not math.isclose(actual, expected, abs_tol=tol)
       for actual, expected in zip(channels, expected_linear_channels)):
    raise SystemExit(1)
# wpctl's scalar output follows the first channel for an unbalanced stream;
# pactl below checks every visible channel independently.
if not math.isclose(wpctl_volume, expected_channels[0], abs_tol=tol):
    raise SystemExit(1)
if len(pulse_channels) != len(expected_channels):
    raise SystemExit(1)
if any(not math.isclose(actual, expected, abs_tol=tol)
       for actual, expected in zip(pulse_channels, expected_channels)):
    raise SystemExit(1)

print(
    f"{label}: node={node} pw-cli(raw={raw:.3f}, channels={channels}) "
    f"wpctl={wpctl_volume:.3f} pactl={pulse_channels}"
)
PY
        then
            return 0
        fi
        sleep 0.05
    done

    echo "state assertion failed: $label" >&2
    pw-cli enum-params "$node" Props >&2 || true
    wpctl get-volume "$node" >&2 || true
    pactl list sink-inputs >&2 || true
    return 1
}

assert_node_sink()
{
    local app=$1
    local expected_sink=$2
    pactl -f json list sink-inputs | python3 -c '
import json
import subprocess
import sys

app, expected = sys.argv[1:]
inputs = json.load(sys.stdin)
item = next(
    (value for value in inputs
     if value.get("properties", {}).get("application.name") == app),
    None,
)
if item is None:
    raise SystemExit(f"missing sink input for {app}")
sinks = json.loads(subprocess.check_output(["pactl", "-f", "json", "list", "sinks"]))
actual = next(
    (value.get("name") for value in sinks if value.get("index") == item.get("sink")),
    None,
)
if actual != expected:
    raise SystemExit(f"{app}: expected sink {expected}, got {actual}")
print(f"{app}: sink={actual}")
' "$app" "$expected_sink"
}

wait_for_daemon()
{
    for _ in $(seq 1 120); do
        "$KVCTL" get volume >/dev/null 2>&1 && return 0
        sleep 0.05
    done
    echo "keyboard-volume-app did not register its private D-Bus service" >&2
    return 1
}

# Start with a deliberately broken external state before the application. The
# startup scan must report it without changing it.
start_stream kv-active
ACTIVE_NODE=$(wait_for_node kv-active)
assert_node_state "initial stream readiness" kv-active 1.0 "1.0,1.0"
pw-cli set-param "$ACTIVE_NODE" Props '{ volume: 0.250000 }' >/dev/null
assert_node_state "pre-existing hidden multiplier" kv-active 0.25 "1.0,1.0"
"$PW_HELPER" inspect "$ACTIVE_NODE" | tee "$TEST_ROOT/production-parser.log"
grep -q "hidden=true" "$TEST_ROOT/production-parser.log"

"$APP" >"$TEST_ROOT/app.log" 2>&1 &
APP_PID=$!
wait_for_daemon
for _ in $(seq 1 120); do
    grep -q "Hidden PipeWire volume multiplier detected" "$TEST_ROOT/app.log" && break
    sleep 0.05
done
grep -q "Hidden PipeWire volume multiplier detected" "$TEST_ROOT/app.log"
assert_node_state "startup detection is read-only" kv-active 0.25 "1.0,1.0"

"$KVCTL" set sink kv-active kv_sink_b
assert_node_state "routing does not normalize hidden state" kv-active 0.25 "1.0,1.0"
assert_node_sink kv-active kv_sink_b
"$KVCTL" set sink kv-active kv_sink_a
assert_node_state "second routing remains read-only" kv-active 0.25 "1.0,1.0"
assert_node_sink kv-active kv_sink_a

"$KVCTL" up
assert_node_state "relative volume up" kv-active 1.0 "0.68,0.68"

"$KVCTL" set volume 45
assert_node_state "absolute kv-ctl/D-Bus volume" kv-active 1.0 "0.45,0.45"

"$KVCTL" scene visible-scene
assert_node_state "scene volume" kv-active 1.0 "0.42,0.42"

dbus-send --session --type=method_call --dest=org.keyboardvolumeapp \
    /org/keyboardvolumeapp org.keyboardvolumeapp.VolumeControl.RefreshApps
sleep 0.7
assert_node_state "refresh is read-only" kv-active 1.0 "0.42,0.42"

"$KVCTL" set volume 61
"$KVCTL" set sink kv-active kv_sink_b
assert_node_state "sink routing preserves volume" kv-active 1.0 "0.61,0.61"
assert_node_sink kv-active kv_sink_b

start_stream kv-inactive
wait_for_node kv-inactive >/dev/null
"$KVCTL" set volume 70 --profile inactive
assert_node_state "inactive setup" kv-inactive 1.0 "0.70,0.70"
stop_stream kv-inactive
wait_for_no_node kv-inactive
"$KVCTL" set volume 35 --profile inactive
start_stream kv-inactive
wait_for_node kv-inactive >/dev/null
assert_node_state "inactive stream restore" kv-inactive 1.0 "0.35,0.35"
stop_stream kv-inactive
wait_for_no_node kv-inactive
start_stream kv-inactive
wait_for_node kv-inactive >/dev/null
assert_node_state "WirePlumber replay" kv-inactive 1.0 "0.35,0.35"

"$KVCTL" set volume 28 --profile pending
start_stream kv-pending
wait_for_node kv-pending >/dev/null
assert_node_state "pending volume" kv-pending 1.0 "0.28,0.28"

start_stream kv-keep
start_stream kv-duck
wait_for_node kv-keep >/dev/null
DUCK_NODE=$(wait_for_node kv-duck)
pw-cli set-param "$DUCK_NODE" Props \
    '{ volume: 0.500000, channelVolumes: [ 0.216000, 0.512000 ] }' >/dev/null
assert_node_state "duck source discrepancy" kv-duck 0.5 "0.60,0.80"
"$KVCTL" duck --profile keep
assert_node_state "ducked stream" kv-duck 1.0 "0.20,0.20"
"$KVCTL" duck --profile keep
assert_node_state "duck restore exact channels" kv-duck 1.0 "0.47622,0.63496"

"$KVCTL" duck --profile keep
assert_node_state "ducked before reconnect" kv-duck 1.0 "0.20,0.20"
for app in kv-active kv-inactive kv-pending kv-keep kv-duck; do
    stop_stream "$app"
done
wait_for_no_node kv-duck
kill "$PULSE_PID"
wait "$PULSE_PID" 2>/dev/null || true
PULSE_PID=
pipewire-pulse >"$TEST_ROOT/pipewire-pulse-reconnect.log" 2>&1 &
PULSE_PID=$!
for _ in $(seq 1 120); do
    [[ -S $XDG_RUNTIME_DIR/pulse/native ]] && pactl info >/dev/null 2>&1 && break
    sleep 0.05
done
pactl info >/dev/null
ensure_test_sinks
start_stream kv-duck
wait_for_node kv-duck >/dev/null
assert_node_state "ducked value replay after reconnect" kv-duck 1.0 "0.20,0.20"
sleep 1.2
"$KVCTL" duck --profile keep
assert_node_state "duck restore after reconnect" kv-duck 1.0 "0.47622,0.63496"

start_stream kv-active kv_sink_b
wait_for_node kv-active >/dev/null
assert_node_state "pipewire-pulse reconnect" kv-active 1.0 "0.61,0.61"
assert_node_sink kv-active kv_sink_b

echo "PipeWire visible-volume integration test passed"
