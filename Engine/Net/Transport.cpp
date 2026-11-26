#include "Net/Transport.h"
#include "Core/Log.h"
#ifndef NOMINMAX
#define NOMINMAX // windows.h (through ENet) would make min / max macros
#endif
#include <enet/enet.h>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace bdg::net {
namespace {
std::map<ENetHost*, Transport*>& Hosts() {
  static std::map<ENetHost*, Transport*> hosts;
  return hosts;
}
} // namespace

bool InitNetworking(std::string* error) {
  static bool ok = false, tried = false;
  static std::mutex m;
  std::lock_guard<std::mutex> lock(m);
  if (!tried) {
    tried = true;
    ok = enet_initialize() == 0;
    if (ok) std::atexit(enet_deinitialize);
  }
  if (!ok && error) *error = "the network could not be started (ENet)";
  return ok;
}

Transport::Transport() = default;
Transport::~Transport() { Close(); }

float Transport::Random01() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 7;
  rng_ ^= rng_ << 17;
  return (float)((rng_ >> 40) & 0xffffff) / 16777216.f;
}

int Transport::Intercept(_ENetHost* host, _ENetEvent*) {
  auto it = Hosts().find(host);
  if (it == Hosts().end()) return 0;
  Transport& t = *it->second;
  if (t.sim.lossPercent > 0 && t.Random01() * 100.f < t.sim.lossPercent) {
    ++t.simDropped_;
    return 1; // consumed: as if it never arrived (ENet resends reliable data)
  }
  return 0;
}

void Transport::Configure(_ENetPeer* p) {
  enet_peer_timeout(p, 0, timeoutMs, timeoutMs);
  enet_peer_ping_interval(p, 250);
}

uint32_t Transport::IdOf(_ENetPeer* p) {
  auto it = ids_.find(p);
  if (it != ids_.end()) return it->second;
  const uint32_t id = server_ ? nextId_++ : 1;
  ids_[p] = id;
  peers_[id] = p;
  return id;
}

bool Transport::Listen(uint16_t port, int maxClients, std::string* error, const std::string& bindAddress) {
  Close();
  if (!InitNetworking(error)) return false;
  ENetAddress a{};
  a.host = ENET_HOST_ANY;
  if (!bindAddress.empty() && enet_address_set_host(&a, bindAddress.c_str()) != 0) {
    if (error) *error = "unknown address " + bindAddress;
    return false;
  }
  a.port = port;
  host_ = enet_host_create(&a, (size_t)std::max(maxClients, 1), 2, 0, 0);
  if (!host_) {
    if (error) *error = "cannot listen on port " + std::to_string(port) + " (in use?)";
    return false;
  }
  ENetAddress bound{};
  port_ = enet_socket_get_address(host_->socket, &bound) == 0 ? bound.port : port;
  server_ = true;
  connected_ = true;
  rng_ = 0x9E3779B97F4A7C15ull ^ sim.seed;
  host_->intercept = &Transport::Intercept;
  Hosts()[host_] = this;
  return true;
}

bool Transport::Connect(const std::string& host, uint16_t port, std::string* error) {
  Close();
  if (!InitNetworking(error)) return false;
  host_ = enet_host_create(nullptr, 1, 2, 0, 0);
  if (!host_) {
    if (error) *error = "cannot open a socket";
    return false;
  }
  ENetAddress a{};
  if (enet_address_set_host(&a, host.c_str()) != 0) {
    if (error) *error = "unknown host " + host;
    Close();
    return false;
  }
  a.port = port;
  ENetPeer* p = enet_host_connect(host_, &a, 2, 0);
  if (!p) {
    if (error) *error = "cannot connect to " + host;
    Close();
    return false;
  }
  Configure(p);
  server_ = false;
  port_ = port;
  rng_ = 0xC2B2AE3D27D4EB4Full ^ sim.seed;
  host_->intercept = &Transport::Intercept;
  Hosts()[host_] = this;
  return true;
}

void Transport::Disconnect(uint32_t peer) {
  if (!host_) return;
  for (auto& [id, p] : peers_)
    if (peer == 0 || id == peer) enet_peer_disconnect_later(p, 1); // after what is queued; 1 = a polite goodbye (a timeout reports 0)
  enet_host_flush(host_);
}

void Transport::Close() {
  if (!host_) return;
  Hosts().erase(host_);
  enet_host_destroy(host_);
  host_ = nullptr;
  peers_.clear();
  ids_.clear();
  delayed_.clear();
  connected_ = false;
  nextId_ = 1;
  std::fill(std::begin(lastReliableAt_), std::end(lastReliableAt_), 0.0);
}

bool Transport::Connected() const { return host_ && connected_; }

std::vector<uint32_t> Transport::Peers() const {
  std::vector<uint32_t> out;
  for (const auto& [id, p] : peers_)
    if (p->state == ENET_PEER_STATE_CONNECTED) out.push_back(id);
  return out;
}

void Transport::Send(uint32_t peer, Channel channel, const void* data, size_t size) {
  if (!host_) return;
  auto it = peers_.find(server_ ? peer : 1);
  if (it == peers_.end() || it->second->state != ENET_PEER_STATE_CONNECTED) return;
  ENetPacket* pk = enet_packet_create(data, size, channel == Channel::Reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
  if (enet_peer_send(it->second, (enet_uint8)channel, pk) != 0) enet_packet_destroy(pk);
}

void Transport::Broadcast(Channel channel, const void* data, size_t size) {
  for (uint32_t id : Peers()) Send(id, channel, data, size);
}

void Transport::Poll(double now, std::vector<Event>& out) {
  if (!host_) return;
  ENetEvent ev;
  int guard = 0;
  while (++guard < 100000 && enet_host_service(host_, &ev, 0) > 0) {
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        Configure(ev.peer);
        Event e;
        e.type = Event::Type::Connected;
        e.peer = IdOf(ev.peer);
        connected_ = true;
        out.push_back(std::move(e));
        break;
      }
      case ENET_EVENT_TYPE_DISCONNECT: {
        Event e;
        e.type = Event::Type::Disconnected;
        e.peer = IdOf(ev.peer);
        e.timedOut = ev.data != 1; // a polite disconnect sends 1 (see Disconnect), a timeout 0
        peers_.erase(e.peer);
        ids_.erase(ev.peer);
        if (!server_) connected_ = false;
        out.push_back(std::move(e));
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE: {
        Event e;
        e.type = Event::Type::Received;
        e.peer = IdOf(ev.peer);
        e.channel = ev.channelID == 0 ? Channel::Reliable : Channel::Unreliable;
        e.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
        enet_packet_destroy(ev.packet);
        if (sim.latencyMs <= 0 && sim.jitterMs <= 0) {
          out.push_back(std::move(e));
        } else {
          double at = now + (sim.latencyMs * 0.5 + sim.jitterMs * (Random01() - 0.5)) / 1000.0;
          if (e.channel == Channel::Reliable) { // keep the order
            double& last = lastReliableAt_[e.peer % 64];
            at = std::max(at, last);
            last = at;
          }
          delayed_.push_back({at, e.peer, e.channel, std::move(e.data), order_++});
        }
        break;
      }
      default: break;
    }
  }
  if (!delayed_.empty()) { // what is due, in time order
    std::stable_sort(delayed_.begin(), delayed_.end(), [](const Delayed& a, const Delayed& b) { return a.at != b.at ? a.at < b.at : a.order < b.order; });
    size_t n = 0;
    while (n < delayed_.size() && delayed_[n].at <= now) {
      Event e;
      e.type = Event::Type::Received;
      e.peer = delayed_[n].peer;
      e.channel = delayed_[n].channel;
      e.data = std::move(delayed_[n].data);
      out.push_back(std::move(e));
      ++n;
    }
    delayed_.erase(delayed_.begin(), delayed_.begin() + (std::ptrdiff_t)n);
  }
  if (now - rateAt_ >= 1.0) { // rates over the last second
    const double span = rateAt_ > 0 ? now - rateAt_ : 1.0;
    sendKbps_ = (float)((host_->totalSentData - rateSent_) * 8 / 1000.0 / span);
    recvKbps_ = (float)((host_->totalReceivedData - rateRecv_) * 8 / 1000.0 / span);
    rateSent_ = host_->totalSentData;
    rateRecv_ = host_->totalReceivedData;
    rateAt_ = now;
  }
}

PeerStats Transport::Stats(uint32_t peer) const {
  PeerStats s;
  if (!host_) return s;
  s.bytesSent = host_->totalSentData;
  s.bytesReceived = host_->totalReceivedData;
  s.sendKbps = sendKbps_;
  s.recvKbps = recvKbps_;
  s.simDropped = simDropped_;
  auto it = peers_.find(server_ ? peer : 1);
  if (it != peers_.end()) {
    s.rttMs = (float)it->second->roundTripTime + sim.latencyMs;
    s.lossPercent = it->second->packetLoss * 100.f / (float)ENET_PEER_PACKET_LOSS_SCALE;
  }
  return s;
}

} // namespace bdg::net
