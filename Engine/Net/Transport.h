#pragma once
// Multiplayer transport (round 7, M-1, Docs/features/multiplayer.md): UDP through ENet (MIT) behind an engine API,
// so nothing above sees ENet. One Transport is a server (Listen, many peers) or a client (Connect, one peer: the
// server). Two channels: Reliable (ordered, resent until acknowledged) and Unreliable (latest wins, may be lost).
//
// Network conditions can be simulated on each end (the editor's Play options, tests): incoming datagrams are dropped
// with `lossPercent` below ENet, so the reliable channel really resends; delivery of what arrives is delayed by half the
// `latencyMs` (both ends set: the round trip grows by latencyMs) plus up to +-jitterMs/2, the reliable channel keeping its
// order. Stats per peer: round trip, loss, bytes and rates.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct _ENetHost;
struct _ENetPeer;
struct _ENetEvent;

namespace bdg::net {

enum class Channel : uint8_t { Reliable = 0, Unreliable = 1 };

struct SimSettings {
  float latencyMs = 0; // added round trip (each end delays what it receives by half)
  float jitterMs = 0;  // +- half of it, per packet
  float lossPercent = 0; // incoming datagrams dropped (0..100)
  uint32_t seed = 1;
};

struct PeerStats {
  float rttMs = 0;        // ENet's measured round trip + the simulated latency
  float lossPercent = 0;  // packets ENet saw lost (resent), smoothed
  uint64_t bytesSent = 0, bytesReceived = 0;
  float sendKbps = 0, recvKbps = 0; // over the last second (whole host)
  uint64_t simDropped = 0;          // datagrams the simulation dropped (whole host)
};

struct Event {
  enum class Type { Connected, Disconnected, Received } type = Type::Received;
  uint32_t peer = 0;              // server: the client's id (1, 2, ...); client: 1 = the server
  Channel channel = Channel::Reliable;
  std::vector<uint8_t> data;
  bool timedOut = false;          // Disconnected: no answer for `timeoutMs` (not a polite goodbye)
};

class Transport {
public:
  Transport();
  ~Transport();
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  // port 0: any free port (Port() tells which). bindAddress "" = every interface.
  bool Listen(uint16_t port, int maxClients, std::string* error = nullptr, const std::string& bindAddress = {});
  bool Connect(const std::string& host, uint16_t port, std::string* error = nullptr); // Connected arrives through Poll
  void Disconnect(uint32_t peer = 0);  // 0 = everyone (server) / the server (client); polite, Disconnected follows
  // At once. goodbye: peers still connected get a last unacknowledged "goodbye" (so they need not time out, e.g. when
  // a polite Disconnect could not finish); false = vanish like a crash (the other end times out: tests).
  void Close(bool goodbye = true);
  void Send(uint32_t peer, Channel channel, const void* data, size_t size);
  void Broadcast(Channel channel, const void* data, size_t size); // server: every connected client
  // Services the network for `now` (seconds, any steady clock) and appends what happened. Call every frame.
  void Poll(double now, std::vector<Event>& out);

  bool IsServer() const { return server_; }
  bool Active() const { return host_ != nullptr; }
  bool Connected() const;                 // client: the server answered; server: listening
  uint16_t Port() const { return port_; }
  std::vector<uint32_t> Peers() const;    // connected peer ids, ascending
  PeerStats Stats(uint32_t peer) const;
  uint32_t timeoutMs = 5000;              // no answer this long = disconnected
  SimSettings sim;

private:
  struct Delayed { double at; uint32_t peer; Channel channel; std::vector<uint8_t> data; uint64_t order; };
  static int Intercept(_ENetHost* host, _ENetEvent* event);
  void Configure(_ENetPeer* p);
  uint32_t IdOf(_ENetPeer* p);
  _ENetHost* host_ = nullptr;
  bool server_ = false;
  uint16_t port_ = 0;
  std::map<uint32_t, _ENetPeer*> peers_;
  std::map<_ENetPeer*, uint32_t> ids_;
  uint32_t nextId_ = 1;
  std::vector<Delayed> delayed_;
  double lastReliableAt_[64] = {};        // per peer slot: reliable delivery times stay in order
  uint64_t order_ = 0;
  uint64_t rng_ = 1;
  uint64_t simDropped_ = 0;
  double rateAt_ = 0;
  uint64_t rateSent_ = 0, rateRecv_ = 0;
  float sendKbps_ = 0, recvKbps_ = 0;
  bool connected_ = false;
  float Random01();
};

// Starts ENet once per process (every Transport calls it).
bool InitNetworking(std::string* error = nullptr);

} // namespace bdg::net
