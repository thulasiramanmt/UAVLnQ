# UAVLnQ layer for the boeing3 shared image (task 1.4.2)
#
# Builds on the team base image (task 1.4.1, PR #87), which already has
# Ubuntu 22.04, ArduCopter SITL, MAVProxy, pymavlink and the ns-3 build tools.
# This layer adds only what UAVLnQ needs on top:
#   - ns-3.44 with UAVLnQ's mesh programs and the MAVLink C library
#   - ZeroMQ, which links the drones to the ns-3 network
#   - Python 3.9 with DroneKit (DroneKit does not run on the base image's Python 3.10)
#
# Build from the team repo, after building the base image:
#   docker compose -f docker-compose.yml -f docker-compose.uavlnq.yml build uavlnq
#
# Nothing in this image needs a display: every interface is a network port or a web page.

ARG BOEING3_BASE_IMAGE=boeing3-base:local
# hadolint ignore=DL3006
FROM ${BOEING3_BASE_IMAGE}
SHELL ["/bin/bash", "-euo", "pipefail", "-c"]
ARG DEBIAN_FRONTEND=noninteractive

USER root

# Team addition (task 1.4.2): Python 3.9 for DroneKit and the ZeroMQ library for the ns-3 bridge.
RUN apt-get update \
 && apt-get install -y --no-install-recommends software-properties-common \
 && add-apt-repository -y ppa:deadsnakes/ppa \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      python3.9 python3.9-venv python3.9-dev libzmq3-dev \
 && rm -rf /var/lib/apt/lists/*

# Team addition (task 1.4.2): C++ ZeroMQ headers, pinned to a release, because Ubuntu 22.04 has no package for them.
ARG CPPZMQ_TAG=v4.10.0
RUN curl -fsSL -o /usr/local/include/zmq.hpp \
        "https://raw.githubusercontent.com/zeromq/cppzmq/${CPPZMQ_TAG}/zmq.hpp" \
 && curl -fsSL -o /usr/local/include/zmq_addon.hpp \
        "https://raw.githubusercontent.com/zeromq/cppzmq/${CPPZMQ_TAG}/zmq_addon.hpp" \
 && install -d -o testbed -g testbed /opt/ns-3 /opt/uavlnq /opt/uavlnq-venv

USER testbed

# Team addition (task 1.4.2): ns-3 at a release tag and the MAVLink C library at a fixed commit, so every build is the same.
ARG NS3_TAG=ns-3.44
ARG MAVLINK_C_COMMIT=8c63a49fe35aead7f9ef14031381c5e076ff743a
RUN git clone --depth 1 --branch "${NS3_TAG}" https://gitlab.com/nsnam/ns-3-dev.git /opt/ns-3 \
 && git clone https://github.com/mavlink/c_library_v2 /opt/ns-3/c_library_v2 \
 && git -C /opt/ns-3/c_library_v2 checkout --quiet "${MAVLINK_C_COMMIT}"

# Team addition (task 1.4.2): build UAVLnQ's three mesh programs into ns-3 with the same options as tools/setup_uavlnq.sh.
COPY --chown=testbed:testbed scratch/3-Leader-Follower-Drone-Mesh /opt/ns-3/scratch/3-Leader-Follower-Drone-Mesh
COPY --chown=testbed:testbed scratch/3-Leader-Follower-Drone-Mesh-with-attacker /opt/ns-3/scratch/3-Leader-Follower-Drone-Mesh-with-attacker
COPY --chown=testbed:testbed scratch/Multi-Leader-Follower-Drone-Mesh /opt/ns-3/scratch/Multi-Leader-Follower-Drone-Mesh
WORKDIR /opt/ns-3
RUN ./ns3 configure --build-profile=optimized --disable-examples --disable-tests \
        --enable-modules="core;network;mobility;wifi;internet;applications;netanim" \
 && ./ns3 build \
 && test -x build/scratch/3-Leader-Follower-Drone-Mesh/ns3.44-three_drone_mesh_normal-optimized

# Team addition (task 1.4.2): UAVLnQ's own Python 3.9 environment, kept apart from the team's Python 3.10 one.
COPY --chown=testbed:testbed requirements.txt /opt/uavlnq/requirements.txt
RUN python3.9 -m venv /opt/uavlnq-venv \
 && /opt/uavlnq-venv/bin/pip install "pip==25.0.1" \
 && /opt/uavlnq-venv/bin/pip install -r /opt/uavlnq/requirements.txt \
 && /opt/uavlnq-venv/bin/python -c "import dronekit, pymavlink, zmq; print('UAVLnQ Python 3.9 environment OK')"

ENV UAVLNQ_NS3_BIN=/opt/ns-3/build/scratch/3-Leader-Follower-Drone-Mesh/ns3.44-three_drone_mesh_normal-optimized \
    UAVLNQ_PYTHON=/opt/uavlnq-venv/bin/python

WORKDIR /workspace
