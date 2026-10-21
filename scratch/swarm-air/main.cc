/*
 * swarm-air: UAVLnQ's ns-3 Wi-Fi mesh used as the radio between the swarm's drone containers.
 *
 * Based on UAVLnQ's scratch/3-Leader-Follower-Drone-Mesh (same 802.11a ad-hoc Wi-Fi, log-distance
 * signal loss and UDP port 20000 inside the simulation), changed so that:
 *   - each drone is its own container on boeing3_net and sends its radio messages here on UDP 15000
 *   - each message is broadcast by that drone's ns-3 node, and is only passed on to the drones whose
 *     ns-3 node actually received it over the simulated Wi-Fi (fixes UAVLnQ finding F6: a message the
 *     radio drops is really lost)
 *   - every node sits where its drone really is (read from the simulator's world folder), so distance
 *     weakens the signal; a drone that is removed moves out of range
 *   - any number of drones (up to --maxDrones) can join or leave while it runs
 *
 * Drone N is ns-3 node N-1 and lives at <prefix>(10+N), the same address plan as the rest of the testbed.
 */

#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <arpa/inet.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace ns3;

namespace
{

const uint16_t SIM_PORT = 20000;     // UDP port inside the simulation (same as UAVLnQ)
const double FAR_AWAY_M = 100000.0;  // where a node waits while its drone is not in the swarm

struct DroneNode
{
    Ptr<Socket> socket;
    Ptr<MobilityModel> mobility;
    bool placed = false;
    uint64_t sentIntoAir = 0;   // messages this drone handed to the radio
    uint64_t heardFromAir = 0;  // messages that reached this drone over the radio
    double lastSeen = 0;        // wall-clock time of its last message
};

std::vector<DroneNode> g_drones;
int g_realSocket = -1;
std::string g_prefix = "172.28.0.";
uint16_t g_realPort = 15000;
std::string g_statusPath;
std::string g_worldDir = "/data/world";

bool g_haveOrigin = false;
double g_lat0 = 0;
double g_lon0 = 0;

double WallNow()
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Pull one number out of the small JSON files in the world folder, e.g. "lat": 37.2296
bool JsonNumber(const std::string& text, const std::string& key, double& value)
{
    size_t at = text.find("\"" + key + "\":");
    if (at == std::string::npos)
    {
        return false;
    }
    value = std::strtod(text.c_str() + at + key.size() + 3, nullptr);
    return true;
}

// MAVLink v1 or v2 frame -> the system id of the drone that sent it.
bool SenderOf(const uint8_t* buf, size_t len, int& sysid)
{
    if (len >= 12 && buf[0] == 0xFD)
    {
        sysid = buf[5];
        return true;
    }
    if (len >= 8 && buf[0] == 0xFE)
    {
        sysid = buf[3];
        return true;
    }
    return false;
}

void MoveAway(int index)
{
    g_drones[index].mobility->SetPosition(Vector(FAR_AWAY_M * (index + 1), 0, 0));
    g_drones[index].placed = false;
}

// Five times a second: put every node where its drone really is. Drones that have gone quiet move out of range.
void UpdatePositions()
{
    double now = WallNow();
    for (size_t i = 0; i < g_drones.size(); i++)
    {
        std::ifstream file(g_worldDir + "/drone" + std::to_string(i + 1) + ".json");
        std::stringstream text;
        text << file.rdbuf();
        double t, lat, lon, alt;
        if (file && JsonNumber(text.str(), "t", t) && now - t < 3.0 && JsonNumber(text.str(), "lat", lat) &&
            JsonNumber(text.str(), "lon", lon) && JsonNumber(text.str(), "rel_alt", alt) && (lat != 0 || lon != 0))
        {
            if (!g_haveOrigin)
            {
                // The first drone seen becomes (0, 0), so there is no hard-coded home location.
                g_lat0 = lat;
                g_lon0 = lon;
                g_haveOrigin = true;
            }
            double x = (lon - g_lon0) * 111320.0 * std::cos(g_lat0 * M_PI / 180.0);
            double y = (lat - g_lat0) * 111320.0;
            g_drones[i].mobility->SetPosition(Vector(x, y, alt));
            g_drones[i].placed = true;
        }
        else if (g_drones[i].placed)
        {
            MoveAway(i);
        }
    }
    Simulator::Schedule(MilliSeconds(200), &UpdatePositions);
}

// A message arrived over the simulated Wi-Fi at drone `index`: hand it to that drone's container.
void ReceiveFromAir(int index, Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        std::vector<uint8_t> data(packet->GetSize());
        packet->CopyData(data.data(), data.size());
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(g_realPort);
        inet_pton(AF_INET, (g_prefix + std::to_string(10 + index + 1)).c_str(), &dst.sin_addr);
        if (sendto(g_realSocket, data.data(), data.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) >= 0)
        {
            g_drones[index].heardFromAir++;
        }
    }
}

// Every 2 ms: take what the drone containers sent and broadcast it from their node in the simulation.
void PollRealSocket()
{
    uint8_t buf[2048];
    ssize_t n;
    while ((n = recv(g_realSocket, buf, sizeof(buf), 0)) > 0)
    {
        int sysid;
        if (!SenderOf(buf, n, sysid))
        {
            continue;
        }
        int index = sysid - 1;
        if (index < 0 || index >= static_cast<int>(g_drones.size()))
        {
            continue;  // check the range before using an id as an index (UAVLnQ finding F20)
        }
        DroneNode& drone = g_drones[index];
        drone.lastSeen = WallNow();
        if (!drone.placed)
        {
            continue;  // no position yet, so the drone is not in the air as far as the radio knows
        }
        drone.socket->SendTo(Create<Packet>(buf, n), 0, InetSocketAddress(Ipv4Address("10.1.1.255"), SIM_PORT));
        drone.sentIntoAir++;
    }
    Simulator::Schedule(MilliSeconds(2), &PollRealSocket);
}

// Once a second, write what the radio is doing to the shared runs volume, for swarm.sh status.
void WriteStatus()
{
    if (!g_statusPath.empty())
    {
        std::ostringstream out;
        out.precision(12);
        out << "{\n  \"node\": \"air\",\n  \"radio\": \"ns-3 802.11a ad-hoc mesh\",\n  \"updated\": " << WallNow()
            << ",\n  \"drones\": {";
        bool first = true;
        for (size_t i = 0; i < g_drones.size(); i++)
        {
            const DroneNode& d = g_drones[i];
            if (!d.placed && d.sentIntoAir == 0)
            {
                continue;
            }
            Vector p = d.mobility->GetPosition();
            out << (first ? "" : ",") << "\n    \"" << i + 1 << "\": {\"in_air\": " << (d.placed ? "true" : "false")
                << ", \"sent\": " << d.sentIntoAir << ", \"received\": " << d.heardFromAir
                << ", \"x\": " << std::round(p.x * 10) / 10 << ", \"y\": " << std::round(p.y * 10) / 10
                << ", \"z\": " << std::round(p.z * 10) / 10 << "}";
            first = false;
        }
        out << "\n  }\n}\n";
        std::string tmp = g_statusPath + ".tmp";
        std::ofstream(tmp) << out.str();
        std::rename(tmp.c_str(), g_statusPath.c_str());
    }
    Simulator::Schedule(Seconds(1), &WriteStatus);
}

} // namespace

int main(int argc, char* argv[])
{
    uint32_t maxDrones = 20;
    double simTime = 1e7;
    double txPowerDbm = 16.0;
    double lossExponent = 3.0;
    CommandLine cmd(__FILE__);
    cmd.AddValue("maxDrones", "Most drones that can join", maxDrones);
    cmd.AddValue("prefix", "Address prefix of the drone containers (drone N = prefix + (10+N))", g_prefix);
    cmd.AddValue("port", "UDP port the drones use, both to send here and to receive", g_realPort);
    cmd.AddValue("world", "Folder with each drone's true position (droneN.json)", g_worldDir);
    cmd.AddValue("status", "Where to write the radio's status JSON (empty = don't)", g_statusPath);
    cmd.AddValue("txPower", "Wi-Fi transmit power in dBm (lower = shorter range)", txPowerDbm);
    cmd.AddValue("lossExponent", "Log-distance path loss exponent (higher = signal fades faster)", lossExponent);
    cmd.AddValue("simTime", "How long to run, in seconds", simTime);
    cmd.Parse(argc, argv);

    // Real time, so the simulated radio keeps pace with the real drones.
    GlobalValue::Bind("SimulatorImplementationType", StringValue("ns3::RealtimeSimulatorImpl"));
    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    NodeContainer nodes;
    nodes.Create(maxDrones);

    // Same radio as UAVLnQ: 802.11a ad-hoc, constant-speed delay, log-distance path loss.
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::AarfWifiManager");
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel", "Exponent", DoubleValue(lossExponent));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(txPowerDbm));
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac", "Ssid", SsidValue(Ssid("drone-mesh")));
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // Every node starts far away from the others, so it can't hear or be heard until its drone is in the swarm.
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> start = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < maxDrones; i++)
    {
        start->Add(Vector(FAR_AWAY_M * (i + 1), 0, 0));
    }
    mobility.SetPositionAllocator(start);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    ipv4.Assign(devices);

    g_drones.resize(maxDrones);
    for (uint32_t i = 0; i < maxDrones; i++)
    {
        Ptr<Socket> s = Socket::CreateSocket(nodes.Get(i), UdpSocketFactory::GetTypeId());
        s->SetAllowBroadcast(true);
        s->Bind(InetSocketAddress(Ipv4Address::GetAny(), SIM_PORT));
        s->SetRecvCallback(MakeBoundCallback(&ReceiveFromAir, static_cast<int>(i)));
        g_drones[i].socket = s;
        g_drones[i].mobility = nodes.Get(i)->GetObject<MobilityModel>();
    }

    // The real side: one UDP socket that every drone container sends its radio messages to.
    g_realSocket = socket(AF_INET, SOCK_DGRAM, 0);
    int yes = 1;
    setsockopt(g_realSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_realPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_realSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::perror("swarm-air: bind");
        return 1;
    }
    fcntl(g_realSocket, F_SETFL, O_NONBLOCK);

    std::cout << "swarm-air: ns-3 Wi-Fi mesh for up to " << maxDrones << " drones, listening on UDP " << g_realPort
              << ", tx power " << txPowerDbm << " dBm, loss exponent " << lossExponent << std::endl;

    Simulator::Schedule(MilliSeconds(2), &PollRealSocket);
    Simulator::Schedule(MilliSeconds(200), &UpdatePositions);
    Simulator::Schedule(Seconds(1), &WriteStatus);
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();
    close(g_realSocket);
    return 0;
}
