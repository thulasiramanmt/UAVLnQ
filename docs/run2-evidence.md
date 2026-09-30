# Run 2 evidence: 3-drone leader-follower baseline (Task 1.1.2 re-run)

**Date:** 2026-09-29.

**Code:** commit `78f81d64`, unmodified source.

**Purpose:** Run 1 (2026-09-28 19:57) never exercised the ns-3 → parser → drone path. This run repeats the baseline with the start order fixed, and with the follower mission files emptied so that any follower movement can only come from the leader's waypoints.

**Result.** The leader → follower path **worked**:

1. ns-3 sent the leader's 6 `MISSION_ITEM`s at t = 20, 30 and 40 s.
2. The parser received all 6 over ZeroMQ 5555.
3. The parser appended them to `mission-drone-2.pln` and `mission-drone-3.pln`.
4. `connect.py` flew drones 2 and 3 to all three waypoints each, at the commanded 30 m altitude.

In the ns-3 mesh, all 6 packets were ACKed by their target node on the first try.

## 1. Setup and procedure

| Step | What was done | Time (UTC) |
|---|---|---|
| Clean start | No leftover `arducopter` / `mavproxy` / `connect.py` / parser / ns-3 processes. Installed `tmux` and `tshark` (3.6.2) | 18:26 |
| SITL | tmux session `uav`, windows `d1`/`d2`/`d3`, each in `~/sitl/dN`: `sim_vehicle.py -v ArduCopter -I{0,1,2} --sysid={1,2,3} -N --out=udp:127.0.0.1:145{5,6,7}1 --out=…2 --out=…3` | start 18:26:50 |
| SITL ready | All three panes show `AP: EKF3 IMU0 is using GPS` | 18:27:44 |
| Mission files | Backed up `mission/*.pln` to the scratch dir. Emptied `mission-drone-2.pln` and `mission-drone-3.pln`. `mission-drone-1.pln` left as committed (`1,20,30,10` / `1,50,30,10` / `1,30,20,10`) | 18:27 |
| Parser **first** | tmux window `parser`: `python -u Mavlink-NS3-Parser.py \| tee logs/parser_run2.log` | started ≈18:27:48 |
| Parser ready | Log shows `Connected to Drone 2`, `Connected to Drone 1`, `Connected to Drone 3` | 18:27:53 |
| Controller | tmux window `connect`: `python -u connect.py \| tee logs/connect_run2.log` (with `~/bin` on `PATH`, `which xterm` → `/home/vscode/bin/xterm`) | 18:27:57 |
| DroneKit connected | `Successfully connected to 3 drones` (well inside the 180 s timeout) | 18:28:36 |
| ns-3 start | `NS-3 PID 3933`. Output prefix `20260929_182843`, so ns-3 t = 0 at 18:28:43 | 18:28:43 |
| ns-3 end | pcaps and NetAnim XML closed (t = 200 s) | 18:32:03 |
| All landed | Last disarm: drone 2 at 18:31:23 | 18:31:23 |
| Stop | Ctrl+C `connect.py` at 18:38:43. Ctrl+C parser and the three `sim_vehicle.py`. `pkill -TERM -x arducopter` for three orphaned SITL binaries. `tmux kill-session` | 18:38:43-18:39 |
| Restore | `git checkout -- mission/`. All four files compared equal (`cmp`) to the backups, then the backups were deleted | after stop |

ns3-output was **not** cleared, to keep the run-1 evidence. Run 2's files are the ones stamped `20260929_182843`.

## 2. Parser: did it append the mission items?

**Yes: 3 lines for drone 2 and 3 for drone 3.** From `logs/parser_run2.log` (25 lines):

```text
14 Received: MISSION_ITEM {target_system : 2, target_component : 0, seq : 0, frame : 3, command : 16, ... x : 50.0, y : 60.0, z : 30.0}
15 Appended Mission Item To mission/mission-drone-2.pln: 2,50.0000000,60.0000000,30.0
16 Received: MISSION_ITEM {target_system : 3, ... x : 50.0, y : 60.0, z : 30.0}
17 Appended Mission Item To mission/mission-drone-3.pln: 3,50.0000000,60.0000000,30.0
18 Received: MISSION_ITEM {target_system : 2, ... x : 10.0, y : 30.0, z : 30.0}
19 Appended Mission Item To mission/mission-drone-2.pln: 2,10.0000000,30.0000000,30.0
20 Received: MISSION_ITEM {target_system : 3, ... x : 20.0, y : 60.0, z : 30.0}
21 Appended Mission Item To mission/mission-drone-3.pln: 3,20.0000000,60.0000000,30.0
22 Received: MISSION_ITEM {target_system : 2, ... x : 60.0, y : 10.0, z : 30.0}
23 Appended Mission Item To mission/mission-drone-2.pln: 2,60.0000000,10.0000000,30.0
24 Received: MISSION_ITEM {target_system : 3, ... x : 20.0, y : 30.0, z : 30.0}
25 Appended Mission Item To mission/mission-drone-3.pln: 3,20.0000000,30.0000000,30.0
```

(`frame : 3` = `MAV_FRAME_GLOBAL_RELATIVE_ALT`; `command : 16` = `MAV_CMD_NAV_WAYPOINT`.)

**Timing.** The parser prints no timestamps, so the append times are derived as follows:

| Pair | ns-3 send time | Wall clock (18:28:43 + t) | Evidence |
|---|---|---|---|
| 1 (lines 15, 17) | t = 20.000 s | 18:29:03 | pcap `frame.time_epoch` 20.000034; drones 2 and 3 were still at N0 E0 at 18:29:02-03 and 48 m away by 18:29:12-13 |
| 2 (lines 19, 21) | t = 30.000 s | 18:29:13 | pcap 30.000034 |
| 3 (lines 23, 25) | t = 40.000 s | 18:29:23 | pcap 40.000034. **Both files' mtime is 18:29:23**, which is when the last append happened |

The per-line times for pairs 1 and 2 are inferred from the pcaps and drone motion, not printed by the parser. Adding a timestamp to the parser output would make this direct (see "Suggested follow-ups").

## 3. New mission file contents

Captured before `git checkout -- mission/` restored the originals:

```text
== mission/mission-drone-2.pln (mtime 18:29:23)
2,50.0000000,60.0000000,30.0
2,10.0000000,30.0000000,30.0
2,60.0000000,10.0000000,30.0
== mission/mission-drone-3.pln (mtime 18:29:23)
3,50.0000000,60.0000000,30.0
3,20.0000000,60.0000000,30.0
3,20.0000000,30.0000000,30.0
```

`mission-drone-1.pln` and `mission-drone-4.pln` were unchanged (mtime 2026-09-28 19:07:20).

## 4. Did drones 2 and 3 fly to those waypoints?

**Yes.** Positions are relative to each drone's first logged fix, in metres North/East, from `logs/drone_N_log_20260929_*.csv`, sampled every 10 s. `connect.py` interprets each line `id,x,y,z` as East = x, North = y, Up = z (`connect.py:304-305`, `mission_commands.py:23`).

| Drone | Start (first CSV row) | Waypoints from ns-3 (E, N, Up) | Observed track | Holding point before RTL | Max horiz / max alt |
|---|---|---|---|---|---|
| 2 | 18:28:52, N0 E0, 9.6 m, GUIDED | (50,60,30) → (10,30,30) → (60,10,30) | 18:29:12 N47.8 E39.9 27 m → 18:29:22 N39.7 E22.7 30 m → 18:29:32 N16.0 E44.3 → 18:29:42 N10.0 E60.1 30 m | **N10.0 E60.0, 30.0 m** = last waypoint (E60, N10) | 78.6 m (≈ √(50²+60²) = 78.1, first waypoint) / 30.1 m |
| 3 | 18:28:53, N0 E0, 9.6 m, GUIDED | (50,60,30) → (20,60,30) → (20,30,30) | 18:29:13 N47.9 E40.0 27 m → 18:29:23 N60.1 E22.7 30 m → 18:29:33 N29.3 E20.0 → 18:29:43 N30.0 E20.0 30 m | **N30.0 E20.0, 30.0 m** = last waypoint (E20, N30) | 78.7 m / 30.1 m |
| 1 (leader, control) | 18:28:51, N0 E0, 9.6 m | none from ns-3. Flew its committed file (20,30), (50,30), (30,20) | ends hold at N20.0 E30.0, 10 m | = last committed line `1,30,20,10` | 58.7 m / 15.1 m (RTL climb) |

Mode changes (from the CSVs), with the `connect_run2.log` messages that explain them:

| Drone | GUIDED hold ends / RTL | LAND | Disarmed | Trigger |
|---|---|---|---|---|
| 1 | 18:30:08 | 18:30:50 | 18:30:54 | Own 60 s idle watchdog (last pop about 18:29:08) |
| 3 | 18:30:24 | 18:31:14 | 18:31:18 | Own 60 s idle watchdog (last pop about 18:29:23) |
| 2 | 18:30:27 | 18:31:19 | 18:31:23 | Own 60 s idle watchdog |

Drone 1 published `MISSION_COMPLETE` after its Land finished (about 18:30:51). The commander then queued a **second** RTL+Land for drones 2 and 3, which were already returning. That is why `connect_run2.log` shows `ReturnHome completed` / `Land completed` twice for drones 2 and 3 (F15).

**Before vs after for drones 2 and 3.** Before, mission files were empty, drones were at home and hovering at 10 m after takeoff. After, both climbed to 30 m, visited the three leader waypoints and held at the last one. The only source of those waypoints was ns-3 → ZMQ 5555 → parser → `.pln` → watchdog. Their files were empty and the parser log shows the appends.

The ns-3 nodes tracked the flights (`~/ns3_output.log`). At t = 60 s node 1 is at (81.66, 14.25, 612.8) = home (21.6, 4.2, 584) + (E60, N10, 29 m), and node 2 at (41.60, 34.19, 612.8) = home + (E20, N30, 29 m).

## 5. ns-3 output

Last 20 lines of `~/ns3_output.log` (597 lines total; no lines containing `error`, `abort` or `terminate`):

```text
Time 193s, Drone 1 Position: (21.606, 4.21903, 584.09)
Time 193s, Drone 2 Position: (21.5969, 4.19676, 584.09)
Time 194s, Drone 0 Position: (21.5606, 4.1745, 584.09)
Time 194s, Drone 1 Position: (21.606, 4.21903, 584.09)
Time 194s, Drone 2 Position: (21.5969, 4.19676, 584.09)
Time 195s, Drone 0 Position: (21.5606, 4.1745, 584.09)
Time 195s, Drone 1 Position: (21.606, 4.21903, 584.09)
Time 195s, Drone 2 Position: (21.5969, 4.19676, 584.09)
Time 196s, Drone 0 Position: (21.5606, 4.1745, 584.09)
Time 196s, Drone 1 Position: (21.606, 4.21903, 584.09)
Time 196s, Drone 2 Position: (21.5969, 4.19676, 584.09)
Time 197s, Drone 0 Position: (21.5606, 4.1745, 584.09)
Time 197s, Drone 1 Position: (21.606, 4.21903, 584.09)
Time 197s, Drone 2 Position: (21.5969, 4.19676, 584.09)
Time 198s, Drone 0 Position: (21.5606, 4.1745, 584.09)
Time 198s, Drone 1 Position: (21.606, 4.21903, 584.09)
Time 198s, Drone 2 Position: (21.5969, 4.19676, 584.09)
Time 199s, Drone 0 Position: (21.5606, 4.1745, 584.09)
Time 199s, Drone 1 Position: (21.606, 4.21903, 584.09)
Time 199s, Drone 2 Position: (21.5969, 4.19676, 584.09)
```

New files in `ns3-output/` (all closed at 18:32:03, t = 200 s):

| File | Size |
|---|---|
| `multi-drone-mesh-20260929_182843-0-0.pcap` | 812,279 B |
| `multi-drone-mesh-20260929_182843-1-0.pcap` | 792,914 B |
| `multi-drone-mesh-20260929_182843-2-0.pcap` | 866,465 B |
| `multi-drone-mesh-anim_20260929_182843.xml` | 4,933,321 B |

New logs: `logs/drone_{1,2,3}_log_20260929_18285{1,2,2}.csv` (1,181 / 1,180 / 1,179 rows, 18:28:51 to 18:38:42), `logs/parser_run2.log`, `logs/connect_run2.log`.

## 6. Were the waypoint packets received by nodes 1 and 2 in the mesh? (tshark)

**Yes, all six, each on the first attempt.** tshark 3.6.2 has no MAVLink dissector, so MISSION_ITEM frames were selected by the MAVLink2 header in the UDP payload (`0xFD`, msgid 39 = `27 00 00` at bytes 7-9):

```bash
tshark -r multi-drone-mesh-20260929_182843-N-0.pcap \
  -Y 'udp.dstport==20000 && data.data[7:3]==27:00:00' \
  -T fields -e frame.number -e frame.time_epoch -e wlan.fc.retry -e wlan.ta -e wlan.ra -e ip.src -e ip.dst
```

Sender side (node 0, 10.1.1.1). Each data frame is followed by an 802.11 ACK (subtype `0x1d`) addressed back to node 0's MAC, which means the receiver got it:

| Frame | ns-3 time (s) | Type | Retry | TA to RA | IP dst |
|---|---|---|---|---|---|
| 958 | 20.000034 | QoS Data | 0 | :01 to :02 | 10.1.1.2 |
| 959 | 20.000126 | **ACK** to :01 | | | |
| 960 | 20.000295 | QoS Data | 0 | :01 to :03 | 10.1.1.3 |
| 961 | 20.000387 | **ACK** to :01 | | | |
| 1418 | 30.000034 | QoS Data | 0 | :01 to :02 | 10.1.1.2 |
| 1419 | 30.000118 | **ACK** to :01 | | | |
| 1420 | 30.000206 | QoS Data | 0 | :01 to :03 | 10.1.1.3 |
| 1421 | 30.000354 | **ACK** to :01 | | | |
| 1840 | 40.000034 | QoS Data | 0 | :01 to :02 | 10.1.1.2 |
| 1841 | 40.000118 | **ACK** to :01 | | | |
| 1842 | 40.000224 | QoS Data | 0 | :01 to :03 | 10.1.1.3 |
| 1843 | 40.000316 | **ACK** to :01 | | | |

Receiver side:

- **Node 1 pcap (10.1.1.2):** frames 969, 1500, 1991 are `10.1.1.1 → 10.1.1.2` MISSION_ITEMs at t ≈ 20, 30, 40 s, all retry=0. It also overheard the three frames for 10.1.1.3.
- **Node 2 pcap (10.1.1.3):** frames 964, 1441, 1925 are `10.1.1.1 → 10.1.1.3` MISSION_ITEMs at t ≈ 20, 30, 40 s, all retry=0. It did not capture node 1's t = 40 s frame, which is not addressed to it.

**Contrast with run 1.** In run 1 the t = 30 s pair and the t = 40 s frame to 10.1.1.2 were each sent 7 times and never ACKed. Retry-flagged UDP-20000 frames overall were about 10 times more frequent in run 1 than here:

| Node | Run 1 retries | Run 2 retries |
|---|---|---|
| 0 | 5,439 | 505 |
| 1 | 5,377 | 511 |
| 2 | 5,402 | 812 |

The cause of that difference was not established. Both runs use the same binary and co-located start positions.

## 7. Issues seen during the run

1. **Ctrl+C in tmux also killed `tee`**, so `connect.py`'s `cleanup()` failed on its first `print` (`connect.py:394`) with `BrokenPipeError`. ns-3 terminate, LAND and ZMQ close therefore did not run. It was harmless here because all drones had landed and ns-3 had already exited. The F14 `context.term()` hang could not be tested for the same reason.
2. `connect.py` did not exit on its own after the mission (F13). It was still running 7 minutes after the last disarm. The ns-3 child stayed `<defunct>` until then because it is never reaped.
3. After Ctrl+C in the `sim_vehicle.py` windows, the three `arducopter` binaries stayed alive and had to be terminated with `pkill -TERM -x arducopter`.
4. The MAVProxy command lines confirm the default extra outputs from F22: `mavproxy.py --out 127.0.0.1:14550 --out 127.0.0.1:14551 …` (and 14560/61, 14570/71).

## 8. Suggested follow-ups (not done; source files were not modified)

- Run `connect.py` with `python -u connect.py > logs/connect.log 2>&1` (no `tee`), or use `tee -i`, so Ctrl+C reaches only Python and `cleanup()` can run.
- Add timestamps to the parser output (e.g. `ts` from `moreutils`, or a `logging` format).
- Re-run with forced mesh loss (e.g. move a follower out of range or add an error model) to demonstrate F6 end to end. This run had no loss, so it cannot show that lost packets still reach the drones.
