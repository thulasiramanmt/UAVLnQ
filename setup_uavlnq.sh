#!/usr/bin/env bash
set -e
UAVLNQ="${UAVLNQ:-/workspaces/UAVLnQ}"
step(){ echo; echo "================ $1 ================"; }
step "1/6 System packages"
sudo apt-get update -y
sudo apt-get install -y software-properties-common git g++ cmake clang ninja-build libzmq3-dev libzmq5
[ -f /usr/include/zmq.hpp ] || { sudo curl -fsSL -o /usr/local/include/zmq.hpp https://raw.githubusercontent.com/zeromq/cppzmq/v4.10.0/zmq.hpp; sudo curl -fsSL -o /usr/local/include/zmq_addon.hpp https://raw.githubusercontent.com/zeromq/cppzmq/v4.10.0/zmq_addon.hpp; }
step "2/6 Python 3.9"
sudo add-apt-repository -y ppa:deadsnakes/ppa
sudo apt-get update -y
sudo apt-get install -y python3.9 python3.9-venv python3.9-dev
step "3/6 ArduPilot SITL (Copter-4.4)"
cd ~
[ -d ardupilot ] || git clone --branch Copter-4.4 --depth 1 --recurse-submodules --shallow-submodules https://github.com/ArduPilot/ardupilot.git
cd ardupilot
Tools/environment_install/install-prereqs-ubuntu.sh -y
set +e; . ~/.profile; set -e
./waf configure --board sitl
./waf copter
grep -q "ardupilot/Tools/autotest" ~/.bashrc || echo 'export PATH="$HOME/ardupilot/Tools/autotest:$HOME/.local/bin:$PATH"' >> ~/.bashrc
step "4/6 ns-3.44 + MAVLink C library"
cd ~
[ -d ns-3-dev ] || git clone --branch ns-3.44 --depth 1 https://gitlab.com/nsnam/ns-3-dev.git
cd ns-3-dev
[ -d c_library_v2 ] || git clone --depth 1 https://github.com/mavlink/c_library_v2
step "5/6 UAVLnQ scripts into ns-3 + build"
for d in 3-Leader-Follower-Drone-Mesh 3-Leader-Follower-Drone-Mesh-with-attacker Multi-Leader-Follower-Drone-Mesh; do cp -r "$UAVLNQ/scratch/$d" scratch/; done
./ns3 configure --build-profile=optimized --disable-examples --disable-tests --enable-modules="core;network;mobility;wifi;internet;applications;netanim"
./ns3 build
step "6/6 UAVLnQ Python environment"
cd "$UAVLNQ"
python3.9 -m venv .venv
. .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
step "CHECKS"
export PATH="$HOME/ardupilot/Tools/autotest:$HOME/.local/bin:$PATH"
echo "sim_vehicle.py : $(which sim_vehicle.py || echo MISSING)"
echo "arducopter     : $(ls ~/ardupilot/build/sitl/bin/arducopter 2>/dev/null || echo MISSING)"
echo "ns-3 programs  :"; ls ~/ns-3-dev/build/scratch/*/ 2>/dev/null | grep -i drone || echo "  MISSING"
"$UAVLNQ/.venv/bin/python" -c "import dronekit, pymavlink, zmq; print('dronekit/pymavlink/zmq import OK')"
echo "ALL DONE - task 1.1.1 install finished"
