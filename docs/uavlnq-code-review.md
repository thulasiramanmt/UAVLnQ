# UAVLnQ code review (Task 1.1.3)

Draft. Review of the fork `thulasiramanmt/UAVLnQ` (upstream `Wh02m1/UAVLnQ`) at commit `78f81d64`, as checked out in `/workspaces/UAVLnQ`.

**How this was produced.** Every claim below cites `file:line` in the files as they are at `78f81d64`. `git status` was clean when this was written. The local edits (the DroneKit `timeout=180` and the 3-drone `drones_config.json`) are already committed in `78f81d64`. Findings are marked:

- **Verified**: the cited code was read and proves the claim. Some are also backed by artefacts from two baseline runs:
  - **run 1**, 2026-09-28 19:57, where the parser was started last;
  - **run 2**, 2026-09-29 18:28, with the correct start order; see [run2-evidence.md](run2-evidence.md).
- **Unverified**: plausible from the code but not proven by reading or by a run.

**Run 1 artefacts used as evidence** (run 2 artefacts are listed in [run2-evidence.md](run2-evidence.md): `*20260929_182843*` pcaps/XML, `logs/drone_*_20260929_*.csv`, `logs/parser_run2.log`, `logs/connect_run2.log`):

| Artefact | What it is |
|---|---|
| `ns3-output/multi-drone-mesh-20260928_195711-{0,1,2}-0.pcap` | One pcap per ns-3 node, 802.11 link type 105, microsecond timestamps |
| `ns3-output/multi-drone-mesh-anim_20260928_195711.xml` | NetAnim trace (`netanim-3.109`) |
| `logs/drone_{1,2,3}_log_20260928_19572{3,3,4}.csv` | DroneKit telemetry, 21 columns, 19:57:23 to 20:05:59 |
| `~/ns3_output.log` | stdout of ns-3, written by the `~/bin/xterm` stand-in (now overwritten by run 2) |
| `~/.bash_history` | The exact commands used |
| `mission/*.pln` | Mission files. Unchanged since the 19:07 clone (mtime 19:07, `git status` clean) |

ns-3 source: the three scratch folders in the repo are byte-identical to `~/ns-3-dev/scratch/…` (`diff -r` shows no differences). `scratch/3-Leader-Follower-Drone-Mesh/CMakeLists.txt:37` builds `ns3.44-three_drone_mesh_normal-optimized`.

---

## a. Purpose and how to run

**Purpose.** UAVLnQ is a co-simulation testbed. Real ArduPilot SITL drones fly (the physics). Their MAVLink telemetry is mirrored into an ns-3 Wi-Fi ad-hoc network model (the network). Commands generated inside ns-3 are fed back to the real drones. It writes PCAPs and NetAnim traces (network view) and CSV telemetry (physical view). The goal is IDS / security research, with an attacker variant (`README.md:3`, `README.md:23`).

**Exact commands used for run 1** (from `~/.bash_history`). Run 1 started the parser **last**, which is why the ns-3 → drone path was not exercised (F8). Use the order in **Correct run procedure** below instead.

```bash
# one-time: headless xterm stand-in, on PATH via ~/.bashrc
cat > ~/bin/xterm <<'EOF'
#!/bin/bash
# headless stand-in for xterm: drop "-hold -e", run the rest, save output to a log
while [ "$1" = "-hold" ] || [ "$1" = "-e" ]; do shift; done
exec "$@" > "$HOME/ns3_output.log" 2>&1
EOF
chmod +x ~/bin/xterm; echo 'export PATH="$HOME/bin:$PATH"' >> ~/.bashrc

# Terminals 1-3: three ArduCopter 4.4 SITL + MAVProxy instances
mkdir -p ~/sitl/d1 && cd ~/sitl/d1 && sim_vehicle.py -v ArduCopter -I0 --sysid=1 -N --out=udp:127.0.0.1:14551 --out=udp:127.0.0.1:14552 --out=udp:127.0.0.1:14553
mkdir -p ~/sitl/d2 && cd ~/sitl/d2 && sim_vehicle.py -v ArduCopter -I1 --sysid=2 -N --out=udp:127.0.0.1:14561 --out=udp:127.0.0.1:14562 --out=udp:127.0.0.1:14563
mkdir -p ~/sitl/d3 && cd ~/sitl/d3 && sim_vehicle.py -v ArduCopter -I2 --sysid=3 -N --out=udp:127.0.0.1:14571 --out=udp:127.0.0.1:14572 --out=udp:127.0.0.1:14573

# Terminal 4: mission controller (also launches ns-3 through "xterm")
cd /workspaces/UAVLnQ && source .venv/bin/activate && python connect.py

# Terminal 5: MAVLink parser
cd /workspaces/UAVLnQ && source .venv/bin/activate && python Mavlink-NS3-Parser.py
```

`drones_config.json` (current, `drones_config.json:1-11`): drones 1-3 on 1455x/1456x/1457x. `ns3_bin` = `/home/vscode/ns-3-dev/build/scratch/3-Leader-Follower-Drone-Mesh/ns3.44-three_drone_mesh_normal-optimized`, `parameters` = `--o=/workspaces/UAVLnQ/ns3-output --simTime=200`.

Both Python programs open `drones_config.json`, `mission/` and `logs/` by **relative path** (`connect.py:21`, `Mavlink-NS3-Parser.py:16`, `connect.py:285`, `Mavlink-NS3-Parser.py:175`, `data_logger.py:15`). They must be started from the repo root.

---

## Correct run procedure (verified in run 2)

Start order: **SITL drones, then the parser, then `connect.py`**. `connect.py` launches ns-3 itself, and ns-3 publishes the leader's waypoints on ZMQ 5555 only 20/30/40 s later. A ZeroMQ PUB socket sends only to subscribers that are already connected and does not keep messages for later joiners. The parser's SUB must therefore be connected before t = 20 s, or those waypoints are lost (F8).

| # | Step | Command | Ready when |
|---|---|---|---|
| 0 | Clean start | `pkill -x arducopter; pkill -f mavproxy.py; pkill -f connect.py; pkill -f Mavlink-NS3-Parser.py; pkill -f three_drone_mesh` | `ps` shows none of them |
| 1 | Fresh mission files | Empty the files the run should fill, e.g. `: > mission/mission-drone-2.pln; : > mission/mission-drone-3.pln`. Restore afterwards with `git checkout -- mission/` (F7) | Files are 0 bytes |
| 2 | SITL ×3 (tmux; MAVProxy needs a TTY) | `tmux new-session -d -s uav -n d1` (plus windows `d2`, `d3`), then in window `dN`: `mkdir -p ~/sitl/dN && cd ~/sitl/dN && sim_vehicle.py -v ArduCopter -I{N-1} --sysid=N -N --out=udp:127.0.0.1:145P1 --out=udp:127.0.0.1:145P2 --out=udp:127.0.0.1:145P3` with P = 5, 6, 7 for drones 1, 2, 3 (full commands in section a) | Every pane shows `AP: EKF3 IMU0 is using GPS` (about 55 s in run 2) |
| 3 | **Parser first** | Window `parser`: `cd /workspaces/UAVLnQ && source .venv/bin/activate && python -u Mavlink-NS3-Parser.py \| tee logs/parser_<run>.log` | Log shows `Connected to Drone 1`, `2` and `3` |
| 4 | Controller (launches ns-3) | Window `connect`: `cd /workspaces/UAVLnQ && source .venv/bin/activate && export PATH=$HOME/bin:$PATH && python -u connect.py \| tee -i logs/connect_<run>.log` (`tee -i`, see F29) | `Successfully connected to 3 drones` (about 40 s), then `NS-3 PID …`. Note the ns-3 output timestamp in `ns3-output/` |
| 5 | Watch | `grep Appended logs/parser_<run>.log` | 6 `Appended` lines by t = 40 s |
| 6 | Finish | ns-3 exits at t = simTime (200 s). Drones land through the 60 s idle-RTL and `MISSION_COMPLETE` | All drones disarmed (CSV `armed=False`). `connect.py` will **not** exit by itself (F13) |
| 7 | Stop | Ctrl+C in `connect`, then `parser`, then `d1`-`d3`. Then `pkill -TERM -x arducopter` (the SITL binaries outlived `sim_vehicle.py` in run 2), and `tmux kill-session -t uav` | No processes left |
| 8 | Restore | `git checkout -- mission/` | `git status` shows no mission changes |

---

## b. Per-file review

### connect.py (mission controller, 438 lines)

**Responsibility.** Connects to every drone twice: DroneKit for control and raw pymavlink for telemetry. It then:

- arms and takes off each drone;
- starts ns-3;
- streams telemetry to ns-3 over ZeroMQ;
- turns lines appended to `mission/mission-drone-N.pln` into flight commands;
- lands everyone when drone 1 finishes.

**Key classes and functions:**

| Item | Lines | Notes |
|---|---|---|
| Module-level ZMQ PUB `tcp://*:5556` | `connect.py:16-18` | Bound at import time |
| Config load | `connect.py:21-34` | `Drones_config`, `NS3_config` |
| Module-level pymavlink connections to `mavlink_connection` (14552/14562/14572) | `connect.py:37-42` | `wait_heartbeat()` with no timeout, sequential |
| `publish_drone_mavlink()` | `connect.py:45-93` | Polls GPS_RAW_INT, SYS_STATUS, HEARTBEAT and publishes `[type, idx, raw]` every 50 ms |
| `DynamicMissionController` | `connect.py:96-204` | Per-drone queue executor. `prepare_drone` (GUIDED, arm, `simple_takeoff(10)`), `execute_mission` loop, 30 s takeoff timeout (`connect.py:197`) |
| Drone 1 only: PUB `tcp://*:5560` | `connect.py:108-111` | Sends `"MISSION_COMPLETE"` when its `Land` finishes (`connect.py:158-160`) |
| `DroneCommander.__init__` | `connect.py:208-219` | SUB `tcp://localhost:5560` |
| `connect_single_drone` / `connect_drones` | `connect.py:221-260` | DroneKit `connect(..., wait_ready=True, heartbeat_timeout=60, timeout=180)` (`connect.py:223`), batches of 5 |
| `start_ns3` | `connect.py:262-281` | `Popen(["xterm","-hold","-e", ns3_bin] + params)` |
| `watchdog` | `connect.py:283-322` | Tails `mission/mission-drone-{id}.pln` from byte 0, parses `id,a,b,c`, queues `MoveToWaypoint(a, b, c)`. Queues RTL+Land if no command has been popped for 60 s |
| `_handle_mission_complete` | `connect.py:324-338` | On `MISSION_COMPLETE`, queues RTL+Land for `controllers[1:]` |
| `start_missions` | `connect.py:340-390` | Order: ns-3, sleep 2 s, telemetry thread, completion thread, then per drone a watchdog and a mission thread |
| `cleanup` | `connect.py:392-427` | Stops controllers, terminates ns-3, sets LAND if armed, closes ZMQ |

**Threads** (for 3 drones):

- main;
- 3 short-lived connect threads (`connect.py:246-253`);
- `publish_drone_mavlink` (daemon, `connect.py:352-356`);
- `_handle_mission_complete` (daemon, `connect.py:359-360`);
- 3 watchdogs (daemon, `connect.py:370-375`);
- 3 `execute_mission` (daemon, `connect.py:380-381`);
- 3 `DataLogger._log_loop` (non-daemon, `data_logger.py:32`);
- DroneKit's own internal threads per vehicle.

**Inputs:**

- `drones_config.json`;
- UDP 14551/61/71 (DroneKit) and 14552/62/72 (pymavlink);
- `mission/mission-drone-N.pln`;
- ZMQ 5560.

**Outputs:**

- MAVLink commands to SITL via DroneKit: mode, arm, takeoff, `SET_POSITION_TARGET_LOCAL_NED`;
- ZMQ PUB 5556 and 5560;
- the ns-3 child process;
- `logs/*.csv` (via DataLogger).

**Issues:** F2, F7, F9, F13-F19 (see section e).

### mission.py (98 lines)

- **Responsibility:** an older, static-list mission executor, `MissionController` (`mission.py:7-98`).
- **Status:** `connect.py:2` imports it but never uses it. `connect.py` uses its own `DynamicMissionController`.
- **Differences from `DynamicMissionController`:**
  - `_takeoff` has **no timeout** (`mission.py:83-91`).
  - `_land_and_disarm` (`mission.py:93-98`) is never called.
  - `import subprocess` is unused (`mission.py:5`).
- **Threads:** none of its own. It starts a `DataLogger` (`mission.py:41-42`).
- **Issues:** dead code (F27).

### mission_commands.py (212 lines)

**Responsibility.** Command objects with a `begin()` / `update()` / `is_done()` protocol, run by the controller loop.

| Class | Lines | Behaviour |
|---|---|---|
| `Command` | `mission_commands.py:13-19` | Interface |
| `MoveToWaypoint(east, north, up, vehicle, tolerance=0.5)` | `mission_commands.py:21-80` | Sends `SET_POSITION_TARGET_LOCAL_NED` (`MAV_FRAME_LOCAL_NED`, position-only mask `0b0000111111111000`, `mission_commands.py:48-58`) on `begin` and on **every** `update`, i.e. every loop tick (~0.1 s, `connect.py:163`). Done when the 3-D distance to the target is under 0.5 m (`mission_commands.py:60-71`). **No timeout** |
| `Sleep` | `mission_commands.py:82-94` | Unused by `connect.py` |
| `ReturnHome(timeout=120)` | `mission_commands.py:96-117` | Sets RTL. Done when alt < 1.0 m or 120 s |
| `Land(timeout=120)` | `mission_commands.py:119-141` | Sets LAND. Done when alt < 0.5 m or 120 s |
| `CheckCommandQueue` | `mission_commands.py:143-186` | Dead code. `COMMAND_QUEUES = None` (`mission_commands.py:10`), so the constructor would raise `TypeError` at `mission_commands.py:148` |
| `MoveToGlobalWaypoint` | `mission_commands.py:188-212` | `simple_goto`. Resend depends on `time.time() % 5 < 0.1` (`mission_commands.py:204`), which is timing-dependent. Only used by the dead `CheckCommandQueue` |

- **Threads:** none.
- **Inputs:** DroneKit `vehicle` state.
- **Outputs:** MAVLink via DroneKit.
- **Issues:** F9, F16, F27.

### data_logger.py (78 lines)

- **Responsibility:** logs DroneKit telemetry to CSV every 0.5 s (`data_logger.py:14`).
- **Output file:** `logs/drone_{id}_log_{YYYYmmdd_HHMMSS}.csv` (`data_logger.py:15`).
- **Header:** 21 columns (`data_logger.py:20-28`). Confirmed against `logs/drone_1_log_20260928_195723.csv` line 1.
- **Threads:** one non-daemon thread per drone (`data_logger.py:32`).
- **File handling:** reopens the file in append mode for every row (`data_logger.py:54`).
- **Issues:**
  - Any exception **ends logging permanently** (`break` at `data_logger.py:76-78`), with only a print.
  - Logging starts only **after** takeoff (`connect.py:133-135`), so arming and the climb are not recorded. Every run log starts at about 9.5-9.8 m altitude.

  See F28.

### Mavlink-NS3-Parser.py (197 lines)

**Responsibility.** The bridge from ns-3 back to the real drones:

- subscribes to ZMQ `tcp://localhost:5555`;
- parses each frame as MAVLink;
- forwards it raw to a QGroundControl UDP port;
- acts on three message types:

| MAVLink message | Action | Lines |
|---|---|---|
| `COMMAND_LONG` | Re-sent to `target_system` over its pymavlink connection (14553/63/73) | `Mavlink-NS3-Parser.py:116-142` |
| `SET_MODE` | Re-sent as `set_mode_send` | `Mavlink-NS3-Parser.py:145-163` |
| `MISSION_ITEM` with `MAV_CMD_NAV_WAYPOINT` | **Appends** `"{target},{x:.7f},{y:.7f},{z:.1f}"` to `mission/mission-drone-{target}.pln`. It does **not** send anything to the drone; `connect.py` does that | `Mavlink-NS3-Parser.py:166-194` |

**Threads:**

- main (busy-poll with 10 ms sleep, `Mavlink-NS3-Parser.py:75-80`);
- one daemon thread per drone that connects, waits for a heartbeat, then idles forever (`Mavlink-NS3-Parser.py:50-72`).

**Inputs:** ZMQ 5555; UDP 14553/63/73 (listen).

**Outputs:**

- UDP to 127.0.0.1:14550/60/70 (QGC);
- MAVLink to SITL on 14553/63/73;
- `mission/*.pln`.

**Issues:**

- QGC routing uses the **source** system of the first message (`Mavlink-NS3-Parser.py:89-93`). ns-3 builds every waypoint with source sysid 1 (`main.cc:107`), so all waypoints are forwarded to **drone 1's** QGC port 14550, including those meant for drones 2 and 3.
- The `drone_connections` membership test runs outside the lock (`Mavlink-NS3-Parser.py:118`, `:147`). This is low risk.
- `CONNECTION_STRINGS` (`Mavlink-NS3-Parser.py:41`) is unused.

See F8, F9, F22.

### drones_config.json (11 lines, local edit)

- **Responsibility:** the single source of ports and the ns-3 command line for both Python programs.
- **Fields per drone** (`drones_config.json:3-5`):
  - `dronekit_connection`, read by `connect.py:25`;
  - `mavlink_connection`, read by `connect.py:26`;
  - `mavlink_parser_connection`, read by `Mavlink-NS3-Parser.py:71`;
  - `qgroundcontrol_port`, read by `Mavlink-NS3-Parser.py:27`.
- **`NS3_config`** (`drones_config.json:7-10`): `connect.py:29-34`.
- **Upstream example** (preserved as `drones_config.original.json`): 4 drones plus `multihop-aodv` with `/home/ubuntu` paths (`drones_config.original.json:23-33`). See F4.
- **Scope:** only ports come from config. The ns-3 program hard-codes 3 drones and ports 5555/5556/20000, and `connect.py` hard-codes 5556/5560 (F10).

### scratch/3-Leader-Follower-Drone-Mesh/main.cc (531 lines)

**Responsibility.** A real-time ns-3 model of 3 drones in an 802.11a ad-hoc network. Its node positions come from the SITL GPS. The leader node sends scheduled waypoint `MISSION_ITEM`s to the followers.

**Network setup:**

- 3 nodes (`main.cc:384`, `main.cc:414`, not a CLI option);
- `ConstantPositionMobilityModel` starting at (0,0,0) (`main.cc:417-424`), moved by `SetPosition` from GPS (`main.cc:220`);
- YANS channel, constant-speed delay, `LogDistancePropagationLossModel` defaults, AARF, `AdhocWifiMac` (`main.cc:433-447`);
- 10.1.1.0/24 (`main.cc:454-456`);
- `RealtimeSimulatorImpl` (`main.cc:411`).

**Key functions:**

| Function | Lines | Behaviour |
|---|---|---|
| `CreateMavlinkPacket` | `main.cc:104-134` | MAVLink2 `MISSION_ITEM`, sysid 1/compid 1, `frame = MAV_FRAME_GLOBAL_RELATIVE_ALT`, `x=lat, y=lon, z=alt` as floats |
| `SendWaypointPairFromDrone0(i)` | `main.cc:137-183` | New UDP socket on node 0 each call (`main.cc:139-140`). Hard-coded waypoints (`main.cc:143-153`). `SendTo` node 1 and node 2 port 20000 (`main.cc:167-168`), **then publishes both packets to ZMQ 5555 regardless** (two-part multipart, `main.cc:175-182`) |
| `ProcessMavlinkMessage` | `main.cc:188-284` | Decodes `[type, idx, raw]` from 5556. GPS updates node `idx` position (`main.cc:202-226`). SYS_STATUS and HEARTBEAT are only logged. Every decoded message is unicast from node `idx` to every other node on UDP 20000 (`main.cc:257-263`) |
| `ZmqPositionReceiverThread` | `main.cc:287-308` | **Separate OS thread**. SUB connect `tcp://localhost:5556`, pushes into a mutex-protected queue |
| `UpdatePositionsFromQueue` | `main.cc:310-323` | Drains the queue every 10 ms of simulated (real) time |
| `PrintDronePositions` | `main.cc:325-338` | 1 Hz stdout; this is the whole content of `~/ns3_output.log` |
| `PacketReceived` / `InstallPacketSinks` | `main.cc:340-381` | `PacketSink` on port 20000 per node. The Rx trace only **logs** GPS_RAW_INT. **No action on `MISSION_ITEM`** |
| Output files | `main.cc:482-506` | `{o}/multi-drone-mesh-{ts}-{node}-0.pcap` (`EnablePcapAll`), `{o}/multi-drone-mesh-anim_{ts}.xml` |

**Threads:** the simulator thread and the ZMQ receiver thread (`main.cc:512`, joined at `main.cc:526`).

**Inputs:** ZMQ 5556; CLI `--simTime --refLat --refLon --refAlt --o` (`main.cc:392-399`).

**Outputs:** ZMQ 5555, pcaps, NetAnim XML, stdout.

**Issues:** F5, F6, F9, F10, F20, F21, F25, F26.

### scratch/3-Leader-Follower-Drone-Mesh-with-attacker (summary)

**`main.cc` (659 lines)** is the baseline plus the following:

- a 4th "Attacker" node at 10.1.1.4, fixed at (100,100,0), with a UDP socket bound to port 5550 (`main.cc:387-397`, `main.cc:447-450`);
- stronger radio: path-loss exponent 2.0 and 40 dBm Tx power (`main.cc:423-427`);
- `--attack` / `--attackTime` (default 50 s) CLI options (`main.cc:355-356`, `main.cc:365-366`) and an if/else dispatch over 11 attacks (`main.cc:484-555`);
- output prefix `multi-drone-mesh-with-Attacker_` (`main.cc:610`, `main.cc:615`);
- the Rx logging trace removed.

**`Attacks/attacks.h` / `attacks.cc` (52 / 927 lines).** MAVLink packet builders plus one `Execute*Attack` per attack (`attacks.cc:503-886`) and `SendWaypointPairFromAttacker` (`attacks.cc:887-927`). Each attack does two things:

1. `SendTo`s the packets from the attacker node, so they appear in the pcaps.
2. Publishes the same bytes to ZMQ 5555 (e.g. `attacks.cc:512-524`), so the parser injects them into SITL (COMMAND_LONG/SET_MODE) or the mission files (MISSION_ITEM).

The physical effect on the drones therefore comes **only from the ZMQ path**, as in F6.

**Observations** (Verified in code, not run):

- Several attacks send to UDP **5551/5552** (`attacks.cc:512-513`, `542-543`, `573-574`, `713-714`, `915-916`). The attacker main only installs sinks on 20000 (`main.cc:469-470`), so those frames are captured but not received by any application.
- `SendWaypointPairFromAttacker` sends every part with `sndmore`, including the last (`attacks.cc:921-922`). See F24.

---

## c. Interfaces

"Confirmed by run" means an artefact from run 1 (2026-09-28) or run 2 (2026-09-29) shows it. "Code only" means only the source shows it.

### UDP (host, 127.0.0.1)

| Port(s) | Direction / role | Producer to consumer | Format | Source | Evidence |
|---|---|---|---|---|---|
| 5760 / 5770 / 5780 (TCP) | SITL serial0 to MAVProxy `--master` | arducopter to MAVProxy | MAVLink2 | `~/ardupilot/Tools/autotest/sim_vehicle.py:855-857` (`5760 + 10*i`) | Code only |
| 14551 / 14561 / 14571 | MAVProxy `--out` to DroneKit (listens: `udp:` = udpin) | MAVProxy (drone N) to `connect.py` DroneKit | MAVLink2 both directions (commands go back to the MAVProxy source port) | `drones_config.json:3-5`, `connect.py:223`; `--out` in `~/.bash_history` | **Confirmed by run**: all three `logs/*.csv` exist with GUIDED/armed rows, so DroneKit connected |
| 14552 / 14562 / 14572 | MAVProxy `--out` to pymavlink telemetry tap | MAVProxy to `connect.py:37-42` | MAVLink2 | `drones_config.json:3-5`, `connect.py:26` | **Confirmed by run**: `~/ns3_output.log` shows all three nodes moving from GPS, which requires this path plus ZMQ 5556 |
| 14553 / 14563 / 14573 | MAVProxy `--out` to parser (listens); parser sends `COMMAND_LONG`/`SET_MODE` back | MAVProxy to/from `Mavlink-NS3-Parser.py:50-58` | MAVLink2 | `drones_config.json:3-5`, `Mavlink-NS3-Parser.py:116-163` | Inbound **confirmed by run 2** (`parser_run2.log`: `Connected to Drone 1/2/3`). Outbound `COMMAND_LONG`/`SET_MODE`: code only (the baseline sends none) |
| 14550 / 14560 / 14570 | QGC ports (nothing listens unless QGC runs) | Parser `sendto`. **Also** MAVProxy by default (`14550+10*i` and `14551+10*i`) | Raw MAVLink | `Mavlink-NS3-Parser.py:22-30`, `89-97`; `sim_vehicle.py:839-851` | MAVProxy default outputs **confirmed in run 2** from the process command lines (`mavproxy.py --out 127.0.0.1:14550 --out 127.0.0.1:14551 …`). Parser sends: code only |
| (duplicate) 14551 / 14561 / 14571 | MAVProxy default extra output **and** the explicit `--out` | Same | | `sim_vehicle.py:839-851` | Code only. Effect of the duplicate output unverified (F22) |

### ns-3 internal (simulated 10.1.1.0/24)

| Port | Role | Producer to consumer | Format | Source | Evidence |
|---|---|---|---|---|---|
| UDP 20000 on 10.1.1.1-3 | Telemetry relay and leader commands | Node `idx` to all other nodes (`main.cc:257-263`); node 0 to nodes 1 and 2 (`main.cc:167-168`) | Raw MAVLink2 (GPS_RAW_INT, SYS_STATUS, HEARTBEAT, MISSION_ITEM) | `main.cc:473-474` | **Confirmed by run**: pcaps contain MISSION_ITEM 10.1.1.1 to 10.1.1.2/.3 at t=20/30/40 s and GPS_RAW_INT forwards |
| UDP 5550 (attacker src) / 5551, 5552 (dst) | Attacker traffic | Attacker to nodes 1 and 2 | MAVLink | attacker `main.cc:450`, `attacks.cc:512-513` | Code only |

### ZeroMQ (TCP on localhost)

| Port | Socket (bind/connect) | Producer | Consumer | Message format | Evidence |
|---|---|---|---|---|---|
| 5556 | PUB **bind** `tcp://*:5556` (`connect.py:17-18`); SUB **connect** `tcp://localhost:5556`, subscribe all (`main.cc:289-291`) | `connect.py` `publish_drone_mavlink` | ns-3 `ZmqPositionReceiverThread` | Single frame: `[msg_type:1 byte (0=GPS_RAW_INT, 1=SYS_STATUS, 2=HEARTBEAT)][drone_idx:1 byte (0-based)][raw MAVLink bytes]` (`connect.py:50-53`, `main.cc:186-194`) | **Confirmed by run** (`~/ns3_output.log` positions change from t=2 s) |
| 5555 | PUB **bind** `tcp://*:5555` (`main.cc:407-408`); SUB **connect** `tcp://localhost:5555`, subscribe all (`Mavlink-NS3-Parser.py:11-13`) | ns-3 `SendWaypointPairFromDrone0` (and attacks) | `Mavlink-NS3-Parser.py` | Two-part multipart per call: frame 1 = raw MAVLink2 `MISSION_ITEM` for sysid 2, frame 2 = the same for sysid 3 (`main.cc:175-182`). pyzmq `recv()` returns each frame separately | **Confirmed by run 2**: `parser_run2.log` shows 6 `Received: MISSION_ITEM` lines (targets 2 and 3, the values in the table in d) and 6 `Appended` lines. Run 1: not delivered, because the parser was not subscribed in time (F8) |
| 5560 | PUB **bind** `tcp://*:5560`, created only for controller with `drone_id == 1` (`connect.py:108-111`); SUB **connect** `tcp://localhost:5560` (`connect.py:217-219`) | `DynamicMissionController` (drone 1) | `DroneCommander._handle_mission_complete` (same process) | UTF-8 string `"MISSION_COMPLETE"` (`connect.py:160`, `329`) | **Confirmed by run 2** (`connect_run2.log`: `Land done - publishing MISSION_COMPLETE` then `Commander: Received MISSION_COMPLETE`). Run 1, indirectly: drone 1 entered LAND at 19:59:22.3 (below 1 m, so RTL done). Drone 3 switched to RTL at 19:59:23.3 without ever having had a command. Its watchdog can't fire without a popped command (`connect.py:315`), so the RTL must have come from `MISSION_COMPLETE` |

### Files

| File | Writer | Reader | Format | Evidence |
|---|---|---|---|---|
| `drones_config.json` | user | `connect.py:21`, `Mavlink-NS3-Parser.py:16` | JSON, see b | Confirmed (used in run) |
| `mission/mission-drone-{id}.pln` | `Mavlink-NS3-Parser.py:174-191` (append); `connect.py:289-290` creates empty | `connect.py` watchdog (`connect.py:285-310`), tail from byte 0 | One line per waypoint: `id,x,y,alt`. Parser writes `%.7f,%.7f,%.1f`. Committed files contain integers (`1,20,30,10`) | Read side **confirmed by runs 1 and 2**: drone 1 flew the committed file in both runs (F7). Write side **confirmed by run 2**: `2,50.0000000,60.0000000,30.0` etc., 3 lines each in `-2.pln` and `-3.pln`, last write 18:29:23 (t = 40 s) |
| `logs/drone_{id}_log_{YYYYmmdd_HHMMSS}.csv` | `data_logger.py:15-28`, `54-72` | none (dataset output) | CSV, 21 columns: `timestamp,lat,lon,alt,velocity_x,velocity_y,velocity_z,ground_speed,air_speed,heading,roll,pitch,yaw,battery_voltage,battery_current,battery_level,mode,armed,system_status,satellites_visible,fix_type` | **Confirmed by run** (3 files, about 1,030 rows each) |
| `ns3-output/multi-drone-mesh-{ts}-{node}-0.pcap` | ns-3 `EnablePcapAll` (`main.cc:485-495`) | Wireshark / IDS | libpcap, magic `0xa1b2c3d4` (microseconds), link type 105 (raw 802.11, no radiotap); UDP 20000 carries MAVLink2 | **Confirmed by runs** (`20260928_195711`, `20260929_182843`) |
| `ns3-output/multi-drone-mesh-anim_{ts}.xml` | ns-3 `AnimationInterface` (`main.cc:490-506`) | NetAnim | XML `<anim ver="netanim-3.109">`, 0.1 s mobility poll, packet metadata on | **Confirmed by run** (9.9 MB) |
| `~/ns3_output.log` | `~/bin/xterm` stand-in (stdout+stderr of ns-3) | user | Text, `Time Ns, Drone i Position: (x, y, z)` | **Confirmed by run** (597 lines, t=1..199 s) |
| attacker: `multi-drone-mesh-with-Attacker_{ts}-*.pcap`, `…-anim_{ts}.xml` | attacker `main.cc:610-615` | | as above | Code only |

---

## d. End-to-end sequence of one run

Wall-clock times are from both runs (file names, CSV timestamps, run-2 logs). Run 1 is 2026-09-28, ns-3 t = 0 at 19:57:11. Run 2 is 2026-09-29, ns-3 t = 0 at 18:28:43, with `mission-drone-2/-3.pln` emptied beforehand and the parser started first. Simulation time *t* is ns-3 real-time seconds since ns-3 started.

1. **SITL up.** Three `sim_vehicle.py` instances start ArduCopter and MAVProxy. Each MAVProxy streams MAVLink to its three `--out` ports, plus its default outputs 14550+10i and 14551+10i (`sim_vehicle.py:839-851`).
2. **`connect.py` import-time setup.** It binds ZMQ PUB 5556 (`connect.py:16-18`), loads the config, and blocks on `wait_heartbeat()` for 14552, 14562 and 14572 in turn (`connect.py:37-42`).
3. **DroneKit connect.** Three threads call `connect(..., wait_ready=True, timeout=180)` and wait until armable (`connect.py:221-260`), then sleep 5 s (`connect.py:435`).
4. **ns-3 launch.** `start_ns3` runs `xterm -hold -e <bin> --o=… --simTime=200` (`connect.py:277-279`). In the Codespace the stand-in execs ns-3 with output to `~/ns3_output.log`.
   - ns-3 binds ZMQ PUB 5555 (`main.cc:407-408`), builds the mesh and starts the 5556 SUB thread (`main.cc:512`).
   - Run: output prefix `20260928_195711`.
5. **Telemetry loop starts** after 2 s (`connect.py:348-356`). `publish_drone_mavlink` then publishes GPS/SYS_STATUS/HEARTBEAT to 5556 every 50 ms. ns-3 moves nodes to the GPS positions (`main.cc:202-226`) and relays each message node-to-node on UDP 20000 (`main.cc:257-263`). Run: nodes jump from (0,0,0) to about (21.6, 4.2, 584) at t=2-3 s (`~/ns3_output.log`).
6. **Per-drone start** (`connect.py:363-384`). For each drone, a watchdog thread starts tailing `mission/mission-drone-N.pln` **from byte 0**, then the mission thread runs `prepare_drone`: GUIDED, arm, `simple_takeoff(10)`, wait for ≥ 9.5 m or 30 s (`connect.py:121-204`). The `DataLogger` then starts (`connect.py:134-135`).
   - Run: CSV logs start 19:57:23-24 at about 9.5-9.8 m.
   - Because the watchdog reads from byte 0, drones 1 and 2 immediately queued the **committed** waypoints from `mission-drone-1.pln` / `-2.pln` (F7).
   - Run 2: logs start 18:28:51-53 at 9.6 m. Only drone 1 had a non-empty file; it flew its committed waypoints again.
7. **Leader's waypoint messages at t = 20, 30, 40 s** (`main.cc:477-479`). Each call to `SendWaypointPairFromDrone0` does two things:
   1. unicasts `MISSION_ITEM` from 10.1.1.1 (sysid 1) to 10.1.1.2 (target 2) and 10.1.1.3 (target 3) on UDP 20000;
   2. **at the same moment** publishes both packets on ZMQ 5555 (`main.cc:167-182`).

   | t | To target 2 | To target 3 |
   |---|---|---|
   | 20 s | (50, 60, 30) | (50, 60, 30) |
   | 30 s | (10, 30, 30) | (20, 60, 30) |
   | 40 s | (60, 10, 30) | (20, 30, 30) |

   The receiving node's sink only logs GPS packets (`main.cc:340-362`), so mesh delivery has no effect.
   - Run 1 (pcaps): t=20 s, both delivered. t=30 s, both frames sent 7 times (original + 6 MAC retries) and absent from the receivers' pcaps. t=40 s, the frame to 10.1.1.2 was sent 7 times and is absent from node 1's pcap.
   - Run 2 (tshark): all 6 frames sent at t = 20.000/30.000/40.000 s with retry = 0, each followed by an 802.11 ACK and present in the target node's pcap.
8. **Parser to mission files.** The parser receives each ZMQ frame, forwards it to QGC 14550 (source sysid 1, `Mavlink-NS3-Parser.py:89-93`), and appends `target,x,y,z` to `mission/mission-drone-{target}.pln` (`Mavlink-NS3-Parser.py:166-194`).
   - Run 1: **no lines were appended** (files unchanged, mtime 19:07), because the parser was not subscribed during t = 20-40 s (F8).
   - Run 2: all 6 received and appended (`parser_run2.log` lines 14-25). Both files were last written at 18:29:23 (t = 40 s).
9. **Watchdog to drone.** Each watchdog notices file growth every 0.5 s and queues `MoveToWaypoint(x, y, z)` as (east, north, up) (`connect.py:296-306`). The mission loop sends `SET_POSITION_TARGET_LOCAL_NED` every 0.1 s until within 0.5 m (`mission_commands.py:45-71`, `connect.py:150-163`).
   - Run 1: drone 1 ended at N20 E30 (committed line `1,30,20,10`) and drone 2 at N20 E230 (`2,230,20,10`), at 10 m. That shows x→east, y→north. Drone 3 never moved (max horizontal 0.1 m).
   - Run 2: drones 2 and 3 climbed to 30 m and visited all three leader waypoints. They held at N10 E60 (drone 2, last item `2,60,10,30`) and N30 E20 (drone 3, `3,20,30,30`), with max horizontal offset 78.6 / 78.7 m (the first waypoint, √(50²+60²) ≈ 78 m). Drone 1 again ended at N20 E30. The ns-3 nodes tracked these positions (`~/ns3_output.log` t = 60 s).
10. **Idle timeout.** If no command is popped for 60 s after at least one pop, the watchdog queues `ReturnHome` + `Land` (`connect.py:312-318`).
    - Run 1: drone 1 RTL at 19:58:40; drone 2 RTL at 19:58:58.
    - Run 2: drone 1 RTL 18:30:08, drone 3 18:30:24, drone 2 18:30:27. All three were triggered by their own watchdog, about 60 s after their last waypoint.
11. **Mission complete.** When drone 1's `Land` finishes (alt < 0.5 m), it publishes `MISSION_COMPLETE` on 5560 (`connect.py:158-160`). `_handle_mission_complete` then queues RTL+Land for drones 2..N (`connect.py:324-333`).
    - Run 1: drone 1 LAND 19:59:22 (landed and disarmed 19:59:27). Drone 3 RTL at 19:59:23.3, its only trigger. Drone 2 received a second RTL+Land (it was already returning). All three were disarmed by 20:00:04.
    - Run 2: drone 1 LAND 18:30:50, then `MISSION_COMPLETE`. Drones 2 and 3 were already returning, so each got a duplicate RTL+Land (logged twice). All disarmed by 18:31:23.
12. **Shutdown.**
    - ns-3 stops at t=200 s (`main.cc:521-528`); run: pcaps closed at 20:00.
    - `connect.py` does **not** exit by itself (F13). The mission threads loop until `cleanup()`, which only runs after the joins return.
    - Run 1: CSV logging continued until 20:05:59, consistent with a manual Ctrl+C at about 20:06 (unverified).
    - Run 2: ns-3 closed its files at 18:32:03 and stayed `<defunct>` (never reaped). `connect.py` was stopped with Ctrl+C at 18:38:43, 7 min after the last disarm. `cleanup()` then failed with `BrokenPipeError` because Ctrl+C also killed `tee` (F29).
    - On a clean exit, `cleanup` stops the controllers, terminates ns-3, sets LAND on armed vehicles and closes ZMQ (`connect.py:392-427`).

---

## e. Findings and risks

Severity is for our capstone MVP: **High** means it breaks a core MVP goal or gives wrong results; **Medium** means it will bite during integration; **Low** means hygiene.

| ID | file:line | Sev | Status | Description | Suggested fix |
|---|---|---|---|---|---|
| F1 | `connect.py:223`; `.venv/lib/python3.9/site-packages/dronekit/__init__.py:3088` | Medium (fixed locally) | **Verified** | DroneKit `connect()` defaults to `timeout=30` (the `wait_ready` budget) and `heartbeat_timeout=30`. With 3 SITLs starting together, parameter download can exceed 30 s. Raised to `timeout=180` in `78f81d64`. That the 30 s limit actually failed for 3 drones is from the user's run; there is no log of the failure. | Make the timeout a config field; connect with retries; log the time taken per drone. |
| F2 | `connect.py:262-279` | High (headless) | **Verified** | ns-3 is started as `xterm -hold -e …`. Without xterm/X11 (Codespaces, Docker) `Popen` raises `FileNotFoundError`, or xterm cannot open a display. Worked around with `~/bin/xterm`, which drops `-hold -e` and redirects to `~/ns3_output.log`. | Launch ns-3 directly: `Popen([ns3_bin, *params], stdout=logfile, stderr=STDOUT)`. Or run it as its own container/service. Keep an optional `--gui` flag for xterm. |
| F3 | `docs/INSTALLATION.md:194`; `docs/INSTALLATION.md:249-250`; `README.md:147` | Low | **Verified** | The install guide copies `ns3-scripts/*`, which does not exist; the scripts are in `scratch/`. The example configs also name non-existent binaries: `NS3-Multi-Drone/ns3.44-NS3-Multi-Drone-default` with `--n=3`, and `ns3.44-drone_mesh-default`. The real names come from `EXECNAME` (e.g. `ns3.44-three_drone_mesh_normal-optimized`; the suffix depends on the build profile). | Fix the docs: `cp -r scratch/* ~/ns-3-dev/scratch/`, list the real binary names per profile, or read `EXECNAME` in a setup script (as `setup_uavlnq.sh` does). |
| F4 | `drones_config.original.json:23-33` (= upstream `drones_config.json`); `main.cc:257-263`, `main.cc:384` | Medium | **Verified** (config); **Unverified** (crash) | The shipped `drones_config.json` has 4 drones and points at `multihop-aodv` (`--nNodes=4 --simTime=100`, `/home/ubuntu` paths), not the documented 3-drone leader-follower program. With 4 entries and the 3-drone binary, `connect.py` publishes `drone_idx = 3`. ns-3 then indexes `g_droneSockets[3]` without a bounds check (the check at `main.cc:213` only guards the GPS branch). This is undefined behaviour and likely a crash. | Ship a 3-drone config matching `README.md`. Validate in ns-3 that `droneId < drones.GetN()` before `main.cc:257`. Pass the drone count from config to ns-3 (`--n`). |
| F5 | `main.cc:384`, `main.cc:414`, `main.cc:137-183`; `README.md:44` | High (for MVP) | **Verified** | There is no ground-control node in the 3-drone ns-3 mesh: only 3 drone nodes exist. Waypoints originate **inside** node 0 on a timer. The README says the leader is "a gateway broker, relaying MAVLink messages … between the GCS and follower UAVs". In the code nothing from a GCS enters the mesh, and nothing from the mesh reaches a GCS except the parser's side copy to QGC ports. | Add a GCS node (wired/Wi-Fi link to node 0). Inject real GCS MAVLink into it via ZMQ. Have node 0 **relay on receipt**. |
| F6 | `main.cc:167-182`, `main.cc:340-362`, `main.cc:364-381` | **High** | **Verified** (code); run evidence partial, see end of description | ns-3 publishes the leader's `MISSION_ITEM`s to ZMQ 5555 **at send time**, in the same function that calls `SendTo`. The receiving nodes' PacketSink trace (`PacketReceived`) only logs GPS_RAW_INT and has no handler for commands. Mesh loss therefore cannot stop a command reaching the real drone, so the network simulation has no causal effect. **Run evidence:** at t=30 s both unicasts, and at t=40 s the unicast to 10.1.1.2, were transmitted 7 times and never appear in the receivers' pcaps. The same packets were still published to 5555. (The cause of the loss with co-located nodes is not established; see F21.) The attacker variant has the same pattern (`attacks.cc:512-524`, etc.).<br><br>**Run 2:** the drones were commanded through exactly this ZMQ path (parser log, CSV tracks). The mesh happened to deliver all 6 packets on the first try, so run 2 neither contradicts nor demonstrates the effect of loss.<br><br>No single run yet shows a **lost** packet still moving a drone. Run 1 had loss but the parser wasn't listening; run 2 had the parser but no loss. That end-to-end demonstration needs a run with forced loss. | Publish to ZMQ from the **receive** callback of the destination node (e.g. `PacketSink` Rx trace or a socket `RecvCallback` on port 20000), keyed by the receiving node. Add a per-message ID so delivery and latency can be measured end-to-end. |
| F7 | `connect.py:285-310`; `mission/mission-drone-1.pln:1-3`, `mission/mission-drone-2.pln:1-3`; `.gitignore` (no `mission/` entry) | **High** | **Verified** (code **and** run) | Mission files are committed to git and never truncated. The watchdog starts at `last_size = 0` (`connect.py:286`), so every run first replays whatever is in the files. **Run evidence:** drone 1's max offset was 58.6 m and it ended at N20 E30; drone 2's max offset was 252.3 m and it ended at N20 E230. These match the committed lines exactly (`1,30,20,10`; `2,230,20,10`). None of the ns-3 waypoints (alt 30 m) were flown. Run 1 did **not** demonstrate the ns-3 to drone path.<br><br>**Run 2 confirms:** drone 1's file was left as committed and it again flew `1,20,30,10` → `1,50,30,10` → `1,30,20,10`, ending at N20 E30. The followers flew only the ns-3 waypoints because their files had been emptied first. | Truncate or rotate `mission/*.pln` at start (in `connect.py` before the watchdogs start, or in the parser). Remove them from git and add `mission/` to `.gitignore`. Better: replace the file hand-off with a ZMQ or DB queue (see f). |
| F8 | `Mavlink-NS3-Parser.py:10-13`, `166-194`; `main.cc:407-408`, `175-182`, `477-479`; `connect.py:347`; run artefacts | Medium (was High) | **Resolved by procedure** (verified by run 2); run-1 root cause **Verified** as consistent, not directly logged | **ZeroMQ late joiner.** ns-3 binds PUB 5555 when `connect.py` launches it (`main.cc:407-408`) and publishes the leader's waypoints at t = 20/30/40 s (`main.cc:175-182`, `477-479`). A PUB socket only sends to subscribers connected at that moment and does not keep messages for later joiners. There is no handshake or retry. If the parser's SUB (`Mavlink-NS3-Parser.py:10-13`) is not connected by t = 20 s, those waypoints are lost for good and the follower mission files stay empty.<br><br>**Run 1:** the parser was started after ns-3 (per the user). The parser wrote nothing (files unchanged, mtime 19:07), although the pcaps prove the MISSION_ITEMs were generated at 19:57:31/41/51. The parser's start time was not logged, so the late join is consistent with, but not directly shown by, run-1 data.<br><br>**Run 2:** the parser was started first and connected at 18:27:53, 50 s before ns-3 started (18:28:43). It received all 6 MISSION_ITEMs and appended them (`logs/parser_run2.log` lines 14-25). The reverse case, parser connecting before ns-3 binds, is fine: ZeroMQ reconnects automatically. | Operational: start SITL → parser → `connect.py` (see **Correct run procedure**) and save parser output. Code: add a readiness handshake (ns-3 waits until the parser is connected; e.g. an XPUB subscription notification, or REQ/REP ready ping). Or use PUSH/PULL, which queues until a peer connects. Or have `connect.py` start the parser itself before ns-3. |
| F9 | `main.cc:115`, `main.cc:123-125`, `main.cc:143-153`; `Mavlink-NS3-Parser.py:170-174`; `connect.py:304-305`; `mission_commands.py:23`, `mission_commands.py:51-53` | Medium | **Verified** (code and run) | Coordinate-frame mismatch. ns-3 labels the waypoints as lat/lon in `MAV_FRAME_GLOBAL_RELATIVE_ALT`, but the values are metres (50, 60…). The parser writes them as `lat,lon` with 7 decimals. `connect.py` passes `lat→east, lon→north` into `MoveToWaypoint(east, north, up)`, which flies them in `MAV_FRAME_LOCAL_NED` relative to the EKF origin. It works only by convention (run: `x`→East, `y`→North). A real lat/lon would be flown as metres (e.g. −35.36 m east). The frame field is wrong for any external tool (QGC, IDS). | Use `MAV_FRAME_LOCAL_NED` (or `LOCAL_OFFSET_NED`) in the MISSION_ITEM, or send real `lat*1e7` in `MISSION_ITEM_INT` with `GLOBAL_RELATIVE_ALT_INT`. Make the parser and controller honour the frame field. |
| F10 | `connect.py:18`, `111`, `218`; `Mavlink-NS3-Parser.py:12`; `main.cc:290`, `408`, `473`, `384`, `143-153`, `477-479`, `84-86`; `connect.py:121`, `197`, `315`, `109`; `mission_commands.py:23` | Medium | **Verified** | Hard-coded values:<br>• ZMQ ports 5555/5556/5560 and mesh port 20000.<br>• Drone count 3 in ns-3 (not a CLI option).<br>• Waypoints and schedule 20/30/40 s.<br>• Reference point CMAC −35.3633, 149.165.<br>• Takeoff 10 m and 30 s takeoff timeout.<br>• 60 s idle-RTL.<br>• Leader = `drone_id == 1`.<br>• 0.5 m tolerance. | Move to config (JSON/env) shared by Python and ns-3 (CLI flags). Load the mission from a file or DB rather than C++ literals. |
| F11 | `requirements.txt:1-4`; `docs/INSTALLATION.md:26`, `33-52` | Medium | **Verified** (pinned versions); **Unverified** (incompatibility) | Needs Python 3.9 and `dronekit==2.9.2`, `pymavlink==2.4.41`, `pyzmq==25.1.1`, `future==0.18.3`. DroneKit 2.9.2 is unmaintained and widely reported to break on Python ≥ 3.10 (`collections.MutableMapping`). Python 3.9 reached end-of-life in Oct 2025. Not tested here on 3.10+. | Short term: pin a `python:3.9-slim` base image. MVP: replace DroneKit with plain pymavlink or MAVSDK on a supported Python. |
| F12 | repo root (no `LICENSE*`/`COPYING*`) | Medium (legal) | **Verified** | No license file. Default copyright applies, so we have no explicit right to copy or modify the code for our product beyond GitHub's fork/view terms. | Ask the authors (README contact / GitHub issue) for a license. Until then, treat it as reference architecture and write our own code, citing the paper (`README.md:7-16`). |
| F13 | `connect.py:138-172`, `174-176`, `386-388`, `436-438` | Medium | **Verified** (code); run consistent | `connect.py` never finishes on its own. `execute_mission` loops while `self.active`, which only `cleanup()` clears, and `cleanup()` runs after `t.join()` on those same threads. Only Ctrl+C ends it. Run 1: logs kept writing until 20:05:59, about 6 min after all drones disarmed. Run 2: same, still running 7 min after the last disarm; the finished ns-3 child stayed `<defunct>` because it is never `wait()`ed until `cleanup()`. | Exit the loop after a final `Land` completes (or on a "mission done" event), then join. |
| F14 | `connect.py:108-111`, `420-427` | Low | **Verified** (socket never closed); **Unverified** (hang) | `complete_pub` (5560) is never closed. pyzmq `Context.term()` blocks until all sockets are closed (default linger), so cleanup may hang after "Terminating NS-3". Could not be tested in run 2 because `cleanup()` aborted earlier (F29). | Close all sockets with `linger=0`, or use `context.destroy(linger=0)`. |
| F15 | `connect.py:312-318`, `324-333`, `108-111` | Medium | **Verified** (code and run) | End-of-mission logic is fragile. The idle-RTL only fires after at least one command has been popped (`last_pop is not None`). A drone with no waypoints (drone 3 in the run) never returns by itself and depends on drone 1's `MISSION_COMPLETE`. If drone 1 never gets a waypoint, nobody lands automatically. Followers are sent home as soon as the leader lands, even mid-task. Duplicate RTL+Land get queued (drone 2 in run 1; drones 2 and 3 in run 2, logged as `ReturnHome completed`/`Land completed` twice). | Use explicit mission state per drone. Start the idle timer at takeoff. Have the leader decide completion from follower acks. De-duplicate RTL. |
| F16 | `mission_commands.py:60-71`; `connect.py:141-163` | Medium | **Verified** | `MoveToWaypoint` has no timeout and a 0.5 m 3-D tolerance. If the target is unreachable (geofence, wind in Gazebo, EKF drift), the queue blocks forever and queued RTL never executes. | Add a timeout and larger tolerance. Use a `MISSION_ITEM_REACHED` or `POSITION_TARGET_LOCAL_NED` check. |
| F17 | `connect.py:62`, `73`, `83`; `.venv/lib/python3.9/site-packages/pymavlink/mavutil.py:530-531` | Medium | **Verified** (code); run consistent | `recv_match(type=X, blocking=False)` reads and **discards** every non-matching message until it finds X. Three consecutive calls per drone therefore throw away most SYS_STATUS/HEARTBEAT (and everything else). ns-3 receives a biased subset of telemetry, which hurts dataset realism for IDS. A rough frame count of the node-0 pcap found about 2,600 GPS_RAW_INT vs about 30 SYS_STATUS and 25 HEARTBEAT over 200 s (rough count, not exact). | A single `recv_match(type=['GPS_RAW_INT','SYS_STATUS','HEARTBEAT'])` loop, or `recv_msg()` and dispatch. Request stream rates explicitly. |
| F18 | `connect.py:259`, `363-365`; `connect.py:57` | Medium | **Verified** | If one DroneKit connection fails, `self.vehicles` drops it and later drones are **renumbered** (`drone_id = idx + 1`). Drone 3 would then read `mission-drone-2.pln`, and whoever is first becomes the "leader" that binds 5560. The telemetry index in `publish_drone_mavlink` still uses config order, so IDs diverge. | Keep the configured `id` with each vehicle. Abort, or continue with explicit gaps. |
| F19 | `connect.py:16-18`, `37-42` | Low | **Verified** | Side effects at import time: binds 5556 and blocks on `wait_heartbeat()` with no timeout, one drone after another. A missing SITL hangs silently before any message saying which one. | Move into `main()`. Use `wait_heartbeat(timeout=…)` and report which endpoint is missing. |
| F20 | `main.cc:193-194`, `213`, `257-263` | Low (Medium with a 4-drone config) | **Verified** | `droneId` from the ZMQ payload is used as an index into `g_droneSockets` without validation. See F4. | Bounds-check and drop invalid IDs. |
| F21 | `main.cc:84-86`, `210`, `215-217`; `~/ns3_output.log` | Low (Medium for MVP realism) | **Verified** (code and run) | Node z = GPS **AMSL** altitude − `refAlt` (default 0), so nodes sit at about 584 m (run log). All three SITLs use the same home (no `--custom-location`/offsets in the commands), so all nodes start co-located (run: identical x,y at t=3 s) and stay within about 250 m. Distance-based loss is therefore barely exercised, so the network model cannot yet show realistic disruption. Why MAC retries still failed at t=30/40 s in run 1 is not established; one hypothesis is contention with the per-message relay traffic from F17/`main.cc:257-263` (unverified). Run 2, with the same binary and start positions, had no command loss and about 10× fewer retry-flagged UDP-20000 frames (505/511/812 vs about 5,400 per node). The loss is therefore not reproducible from geometry alone; its cause is still open. | Set `--refAlt` from home altitude or use relative altitude. Start SITLs at separated homes (`--custom-location`) or enable `SIM_` offsets. Add scenario knobs (distance, loss model, jammer). |
| F22 | `Mavlink-NS3-Parser.py:89-97`; `~/ardupilot/Tools/autotest/sim_vehicle.py:839-851` | Low | **Verified** (code); **Unverified** (impact) | QGC ports receive two independent streams: MAVProxy's default `--out 127.0.0.1:14550+10i` and the parser's copies of ns-3 frames. The parser routes by **source** sysid, so leader commands for drones 2 and 3 appear on drone 1's QGC port. MAVProxy also outputs to 14551+10i by default, the same port as the explicit DroneKit `--out`; the effect of the duplicate on DroneKit is untested. Run 2 confirmed the extra outputs from the MAVProxy process command lines. DroneKit connected normally despite them. | Pass `--no-extra-ports` to `sim_vehicle.py`. Route by `target_system` for commands. |
| F23 | `.gitignore:1-18`; commit `78f81d64` | Low | **Verified** | Run artefacts (`logs/*.csv`, about 3,100 lines) are committed and `mission/` is tracked. `logs/` is not in the root `.gitignore` (only `scratch/.gitignore` lists it). | Add `logs/` and `mission/` to `.gitignore`. Keep sample outputs in a separate `samples/` folder if wanted. |
| F24 | `scratch/3-Leader-Follower-Drone-Mesh-with-attacker/Attacks/attacks.cc:919-922` | Medium (attacker scenario) | **Verified** (code); **Unverified** (runtime) | `SendWaypointPairFromAttacker` sends both frames with `zmq::send_flags::sndmore`, including the last. ZeroMQ only delivers a multipart message after the final part, so the MissionInjection waypoints may never reach the parser until some later send without `sndmore`. | Send the last part with `send_flags::none`. |
| F25 | attacker `main.cc:469-470`; `attacks.cc:512-513`, `542-543`, `573-574`, `713-714`, `915-916` | Low | **Verified** | Several attacks target UDP 5551/5552, but sinks exist only on 20000. The frames are in the pcap, but no simulated node "receives" them. The real effect is only via ZMQ (same as F6). | Target port 20000, or add sinks. Route effects through receive callbacks. |
| F26 | `main.cc:139-140`, `main.cc:346-347` | Low | **Verified** | A new UDP socket is created per waypoint call and never closed. `PacketReceived` copies at most 256 bytes, but MAVLink2 frames can be up to 280 (`MAVLINK_MAX_PACKET_LEN`). | Reuse `g_droneSockets[0]`. Size the buffer to `p->GetSize()`. |
| F27 | `mission.py:1-98`; `connect.py:2-3`; `mission_commands.py:10`, `143-212` | Low | **Verified** | Dead code: `MissionController`, `Sleep`, `CheckCommandQueue` (would raise on construction because `COMMAND_QUEUES` is `None`), `MoveToGlobalWaypoint`. | Delete, or move to `legacy/`. |
| F28 | `data_logger.py:54`, `76-78`; `connect.py:133-135` | Low | **Verified** | The logger reopens the file for each row. A single exception stops logging for the rest of the run. Logging starts only after takeoff, so the arm/climb phase is missing from the dataset. | Keep the file open (or write to a DB), `continue` on error with a counter, and start logging before arming. |
| F29 | `connect.py:392-394`, `430-438` | Low | **Verified** (run 2) | Running `python connect.py \| tee log` and pressing Ctrl+C in the terminal sends SIGINT to both processes. `tee` exits, and the first `print` in `cleanup()` (`connect.py:394`) raises `BrokenPipeError`. The rest of cleanup (stop controllers, terminate ns-3, LAND armed vehicles, close ZMQ) is then skipped. Seen in run 2; harmless there only because everything had already landed and exited. | Use `tee -i`, or redirect (`> log 2>&1`). In code, make `cleanup()` robust: use `logging`, and wrap each step in its own `try`. |

---

## f. Reuse vs. change for our MVP

MVP requirements:

- (R1) the leader relays between ground control and followers;
- (R2) network disruption actually affects delivery;
- (R3) runs in Docker, headless;
- (R4) database logging.

| Area | Reuse as-is | Reuse with changes | Replace / new |
|---|---|---|---|
| SITL + MAVProxy launch (`sim_vehicle.py -I{n} --sysid={n+1} --out …`) | ✓ port scheme 145{5+n}{1,2,3} | Add `--no-extra-ports` (F22) and separate home locations (F21) | Containerise one SITL per container (R3) |
| Telemetry bridge SITL → ns-3 (`connect.py:45-93`, ZMQ 5556, `[type, idx, raw]` framing, `main.cc:186-323`) | ✓ The framing and the realtime simulator with a queue drained on the simulator thread are a good pattern | Fix message dropping (F17); drone count and ports from config (F10); validate IDs (F20) | — |
| ns-3 mesh model (`main.cc:414-474`) | ✓ Wi-Fi ad-hoc, IP plan, GPS-driven positions | Correct altitude (F21); configurable N; add loss/jamming knobs (R2) | **Add a GCS node** linked to the leader (R1, F5) |
| Leader relay logic | — | — | **New.** The GCS sends to leader node 0; node 0's receive callback forwards to followers; each follower's **receive callback** publishes to ZMQ (R1, R2, F6). Commands then only reach SITL if the mesh delivered them. |
| ns-3 → drone command path (ZMQ 5555 → parser → `.pln` file → watchdog → DroneKit) | ZMQ 5555 as the transport | Parser's `COMMAND_LONG`/`SET_MODE` forwarding can be kept | **Replace the file hand-off** (F7, F8) with a direct ZMQ PUSH/PULL (or a DB queue) to the controller. Use a correct frame (F9). |
| Mission controller (`DynamicMissionController`, `mission_commands.py`) | Queue plus `begin/update/is_done` pattern | Timeouts (F16), clean completion and exit (F13, F15), stable IDs (F18) | Consider pymavlink or MAVSDK instead of DroneKit (F11) |
| ns-3 launch | — | Launch without xterm (F2), write stdout to a log | Run ns-3 as its own container (R3) |
| Logging (`data_logger.py`, pcaps, NetAnim) | pcap and NetAnim output | CSV logger fixes (F28) | **DB logging** (R4): one table each for telemetry (the 21 CSV columns plus `run_id`, `drone_id`), commands (send time, receive time, delivered y/n), and runs (config, git SHA). Can be fed from the same threads. |
| Attacker scenario | Packet builders in `Attacks/attacks.cc` are reusable | Route effects through receive callbacks (F6, F25); fix `sndmore` (F24) | — |
| Configuration (`drones_config.json`) | Schema for drone ports | Add ns-3 count, ZMQ ports, mission, reference point (F10); fix sample (F4) | — |
| Licensing | — | — | Clarify the license before copying code (F12) |
