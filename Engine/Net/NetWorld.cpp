#include "Net/NetWorld.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
#include "Core/Space.h"
#include "Net/NetComponents.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
#include "Scripting/ScriptApi.h"
#include <yyjson.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <format>

namespace bdg {
namespace {
using net::BitReader;
using net::BitWriter;
using net::Channel;
using net::NetTransform;

enum Msg : uint32_t { kHello = 1, kWelcome, kSpawn, kDespawn, kFields, kSnapshot, kAck, kMove, kRpc, kPlayers, kReject };
constexpr uint32_t kProtocol = 2; // 2: hello carries the content list
std::string ContentText(const std::vector<ContentEntry>& list) {
  std::string s;
  for (const ContentEntry& e : list) s += (s.empty() ? "" : ", ") + e.name + " " + e.hash.substr(0, 12);
  return s.empty() ? std::string("(none)") : s;
}
constexpr double kSnapshotRate = 30.0;

NetWorld* g_current = nullptr;

template <class T> T* Comp(SceneObject* o, const TypeInfo& type) {
  if (!o) return nullptr;
  for (Component& c : o->components)
    if (c.type == &type) return static_cast<T*>(c.data);
  return nullptr;
}
template <class T> const T* Comp(const SceneObject* o, const TypeInfo& type) { return Comp<T>(const_cast<SceneObject*>(o), type); }

NetTransform Read(const Transform& t) {
  NetTransform n;
  n.position = {t.position.x, t.position.y, t.position.z};
  n.rotation = space::EulerDegreesToQuat(t.rotation);
  n.scale = {t.scale.x, t.scale.y, t.scale.z};
  return n;
}
void Write(Transform& t, const NetTransform& n) {
  t.position = {n.position.x, n.position.y, n.position.z};
  t.rotation = space::QuatToEulerDegrees(n.rotation);
  t.scale = {n.scale.x, n.scale.y, n.scale.z};
}
// Objects in hierarchy order (parents first): the same order on every machine.
std::vector<SceneObject*> AllObjects(Scene& scene) {
  std::vector<SceneObject*> out;
  std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
  while (!stack.empty()) {
    SceneObject* o = scene.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    out.push_back(o);
    stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
  }
  return out;
}
std::vector<uint8_t> Packet(const BitWriter& w) { return w.Data(); }
} // namespace

const char* NetModeName(NetMode m) {
  switch (m) {
    case NetMode::Standalone: return "Standalone";
    case NetMode::ListenServer: return "Listen Server";
    case NetMode::DedicatedServer: return "Dedicated Server";
    case NetMode::Client: return "Client";
  }
  return "?";
}

struct NetWorld::Client {
  uint32_t id = 0;
  std::string name;
  bool welcomed = false;
  uint32_t ackSeq = 0;
  std::map<uint32_t, std::map<uint32_t, NetTransform>> sent; // snapshot seq -> what it carried
  std::map<uint32_t, uint32_t> moveSeq;                       // net id -> last applied move
  std::map<uint32_t, double> moveTime;                        // net id -> server time of the last accepted move
};

NetWorld::NetWorld() = default;
NetWorld::~NetWorld() { Stop(); }
NetWorld* NetWorld::Current() { return g_current; }

bool NetWorld::Start(Scene& scene, CommandBus&, const NetSettings& s, std::string* error) {
  Stop();
  settings_ = s;
  transport_.sim = s.sim;
  count_ = {};
  players_.clear();
  if (s.mode == NetMode::Standalone) return true;
  if (IsServer()) {
    if (!transport_.Listen(s.port, s.maxClients, error, s.address)) return false;
    localId_ = 0;
    // what the world already has: every NetworkObject gets its net id now (clients get them in the scene text)
    for (SceneObject* o : AllObjects(scene))
      if (NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType())) {
        no->netId = (int32_t)nextNetId_++;
        netIds_[o->id] = (uint32_t)no->netId;
      }
    for (auto& [object, id] : netIds_)
      for (const Component& c : scene.Find(object)->components) fieldsSent_[object + "|" + c.type->name] = FieldsOf(scene, object, c.type->name);
    if (s.mode == NetMode::ListenServer) players_.push_back({0, s.playerName});
    LogInfo("net: {} on port {}", NetModeName(s.mode), transport_.Port());
  } else {
    if (!transport_.Connect(s.address, s.port, error)) return false;
    LogInfo("net: client connecting to {}:{}", s.address, s.port);
  }
  g_current = this;
  return true;
}

void NetWorld::Stop() {
  if (transport_.Active()) {
    // a polite goodbye: wait (briefly) until the other end has it, so it does not have to time out
    transport_.Disconnect();
    std::vector<net::Event> ignore;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(IsServer() ? 100 : 500);
    size_t goodbyes = 0;
    const size_t expected = IsServer() ? clients_.size() : 1;
    while (goodbyes < expected && std::chrono::steady_clock::now() < until) { // ENet resends it while we wait
      transport_.Poll(clock_, ignore);
      for (const net::Event& e : ignore) goodbyes += e.type == net::Event::Type::Disconnected;
      ignore.clear();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    transport_.Close();
    LogInfo("net: stopped ({} spawned, {} snapshots, {} moves, {} RPCs)", count_.spawned, count_.snapshots, count_.moves, count_.rpcs);
  }
  if (g_current == this) g_current = nullptr;
  clients_.clear();
  netIds_.clear();
  fieldsSent_.clear();
  objectOf_.clear();
  buffers_.clear();
  received_.clear();
  owned_.clear();
  players_.clear();
  joined_ = helloSent_ = haveOffset_ = false;
  nextNetId_ = 1;
  snapshotSeq_ = lastSeq_ = 0;
  sendAcc_ = 0;
  localId_ = 0;
}

bool NetWorld::IsOwner(const Scene& scene, const std::string& object) const {
  const NetworkObject* no = Comp<NetworkObject>(scene.Find(object), NetworkObject::StaticType());
  return no ? (uint32_t)no->owner == localId_ : settings_.mode != NetMode::Client;
}
bool NetWorld::HasAuthority(const Scene& scene, const std::string& object) const {
  (void)scene;
  (void)object;
  return settings_.mode != NetMode::Client;
}

net::PeerStats NetWorld::Stats() const {
  net::PeerStats worst;
  for (uint32_t p : transport_.Peers()) {
    const net::PeerStats s = transport_.Stats(p);
    if (s.rttMs >= worst.rttMs) worst = s;
  }
  if (transport_.Peers().empty()) worst = transport_.Stats(1);
  return worst;
}

std::string NetWorld::Describe() const {
  const net::PeerStats s = Stats();
  return std::format("{}, {} player(s), ping {:.0f} ms, loss {:.1f} %, {:.1f} / {:.1f} kbit/s", NetModeName(settings_.mode), players_.size(), s.rttMs,
                     s.lossPercent, s.sendKbps, s.recvKbps);
}

std::string NetWorld::FieldsOf(const Scene& scene, const std::string& object, const std::string& type) const {
  const SceneObject* o = scene.Find(object);
  if (!o) return {};
  for (const Component& c : o->components) {
    if (c.type->name != type) continue;
    std::string json;
    for (const FieldInfo& f : c.type->fields) {
      if (!(f.flags & kReplicated)) continue;
      json += std::format("{}\"{}\":{}", json.empty() ? "" : ",", f.name, Registry::ToJson(static_cast<const unsigned char*>(c.data) + f.offset, f.value));
    }
    return json.empty() ? std::string() : "{" + json + "}";
  }
  return {};
}

// ---- receiving ----
void NetWorld::Receive(Scene& scene, CommandBus& bus, double now) {
  clock_ = now;
  if (!transport_.Active()) return;
  std::vector<net::Event> events;
  transport_.Poll(now, events);
  for (const net::Event& e : events) {
    if (IsServer()) HandleServer(scene, bus, e, now);
    else HandleClient(scene, bus, e, now);
  }
}

void NetWorld::HandleServer(Scene& scene, CommandBus& bus, const net::Event& e, double now) {
  using T = net::Event::Type;
  if (e.type == T::Connected) {
    auto c = std::make_unique<Client>();
    c->id = e.peer;
    clients_[e.peer] = std::move(c);
    return;
  }
  if (e.type == T::Disconnected) {
    if (clients_.count(e.peer)) {
      const bool wasPlayer = clients_[e.peer]->welcomed;
      LogInfo("net: player {} left{}", e.peer, e.timedOut ? " (timed out)" : "");
      clients_.erase(e.peer);
      if (wasPlayer) PlayerLeft(scene, bus, e.peer);
    }
    return;
  }
  auto it = clients_.find(e.peer);
  if (it == clients_.end()) return;
  Client& c = *it->second;
  BitReader r(e.data);
  const uint32_t type = r.Bits(8);
  if (type == kHello) {
    const uint32_t protocol = (uint32_t)r.VarUint();
    c.name = r.String();
    std::vector<ContentEntry> content(protocol >= 2 ? (size_t)std::min<uint64_t>(r.VarUint(), 256) : 0);
    for (ContentEntry& ce : content) {
      ce.name = r.String();
      ce.hash = r.String();
    }
    std::string why;
    if (protocol != kProtocol || r.Overflowed()) why = std::format("a different engine version (network protocol {} here, {} there)", kProtocol, protocol);
    else if (content.size() != settings_.content.size() ||
             !std::equal(content.begin(), content.end(), settings_.content.begin(), [](const ContentEntry& x, const ContentEntry& y) { return x.name == y.name && x.hash == y.hash; }))
      why = std::format("different game content (server: {}; yours: {})", ContentText(settings_.content), ContentText(content));
    if (!why.empty()) {
      BitWriter no;
      no.Bits(kReject, 8);
      no.String(why);
      transport_.Send(e.peer, Channel::Reliable, no.Data().data(), no.Data().size());
      transport_.Disconnect(e.peer);
      ++count_.rejected;
      LogWarn("net: turned away a player: {}", why);
      return;
    }
    if (c.name.empty()) c.name = std::format("Player {}", c.id);
    // spawn its player first, so the world it receives has it (with its net id)
    SpawnPlayer(scene, bus, c.id);
    ServerSend(scene, bus, now, 0); // net ids for the new objects; spawns to the others
    Welcome(scene, c.id);
    c.welcomed = true;
    players_.push_back({c.id, c.name});
    BroadcastPlayers();
    LogInfo("net: player {} \"{}\" joined", c.id, c.name);
    if (onPlayer) onPlayer(c.id, true);
  } else if (type == kAck) {
    const uint32_t seq = (uint32_t)r.VarUint();
    if (!r.Overflowed() && (int32_t)(seq - c.ackSeq) > 0) {
      c.ackSeq = seq;
      for (auto s = c.sent.begin(); s != c.sent.end();) s = (int32_t)(s->first - seq) < 0 ? c.sent.erase(s) : std::next(s);
    }
  } else if (type == kMove) {
    const uint32_t netId = (uint32_t)r.VarUint(), seq = (uint32_t)r.VarUint();
    glm::vec3 delta;
    delta.x = r.Float(); // one by one: arguments of one call are read in no fixed order
    delta.y = r.Float();
    delta.z = r.Float();
    if (r.Overflowed()) return;
    std::string object;
    for (auto& [obj, id] : netIds_)
      if (id == netId) object = obj;
    SceneObject* o = scene.Find(object);
    const NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType());
    const NetworkTransform* nt = Comp<NetworkTransform>(o, NetworkTransform::StaticType());
    Transform* tr = Comp<Transform>(o, Transform::StaticType());
    if (!no || !nt || !tr || (uint32_t)no->owner != c.id || !nt->ownerPredicted) { ++count_.rejected; return; }
    // a move may not be faster than maxSpeed since the last one (with a little slack for bunched packets)
    const double last = c.moveTime.count(netId) ? c.moveTime[netId] : now - 0.1;
    const float allowed = nt->maxSpeed * (float)std::clamp(now - last + 0.05, 0.0, 0.5);
    const float len = glm::length(delta);
    if (len > allowed && len > 0) {
      delta *= allowed / len;
      ++count_.corrections;
    }
    c.moveTime[netId] = now;
    tr->position = {tr->position.x + delta.x, tr->position.y + delta.y, tr->position.z + delta.z};
    c.moveSeq[netId] = seq;
    ++count_.moves;
  } else if (type == kRpc) {
    const std::string object = r.String(), comp = r.String(), method = r.String(), args = r.String();
    const RpcKind kind = (RpcKind)r.Bits(2);
    if (r.Overflowed()) return;
    const NetworkObject* no = Comp<NetworkObject>(scene.Find(object), NetworkObject::StaticType());
    if (kind != RpcKind::Server || !no || (uint32_t)no->owner != c.id) { // only the owner may ask the server
      ++count_.rejected;
      LogWarn("net: player {} may not call {}.{} on {} (not its owner)", c.id, comp, method, object);
      return;
    }
    DeliverRpc(object, comp, method, args, c.id);
  }
}

// Our own predicted objects start from where they are when they reach us (the first local move counts).
static void StartOwned(Scene& scene, const std::map<uint32_t, std::string>& objectOf, uint32_t localId, auto& owned) {
  for (auto& [netId, object] : objectOf) {
    SceneObject* o = scene.Find(object);
    const NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType());
    const NetworkTransform* nt = Comp<NetworkTransform>(o, NetworkTransform::StaticType());
    const Transform* tr = Comp<Transform>(o, Transform::StaticType());
    if (!no || !nt || !tr || (uint32_t)no->owner != localId || !nt->ownerPredicted || owned.count(netId)) continue;
    auto& own = owned[netId];
    own.started = true;
    own.last = {tr->position.x, tr->position.y, tr->position.z};
    own.prediction.Reset(own.last);
  }
}

void NetWorld::HandleClient(Scene& scene, CommandBus& bus, const net::Event& e, double now) {
  using T = net::Event::Type;
  if (e.type == T::Connected) {
    BitWriter w;
    w.Bits(kHello, 8);
    w.VarUint(kProtocol);
    w.String(settings_.playerName);
    w.VarUint(settings_.content.size());
    for (const ContentEntry& ce : settings_.content) {
      w.String(ce.name);
      w.String(ce.hash);
    }
    transport_.Send(1, Channel::Reliable, w.Data().data(), w.Data().size());
    helloSent_ = true;
    return;
  }
  if (e.type == T::Disconnected) {
    LogWarn("net: the server went away{}", e.timedOut ? " (timed out)" : "");
    joined_ = false;
    if (onDisconnected) onDisconnected(e.timedOut ? "timed out" : "disconnected");
    return;
  }
  BitReader r(e.data);
  const uint32_t type = r.Bits(8);
  if (type == kReject) {
    rejectReason_ = r.String();
    LogWarn("net: the server turned us away: {}", rejectReason_);
    if (onDisconnected) onDisconnected(rejectReason_);
    return;
  }
  if (type == kWelcome) {
    localId_ = (uint32_t)r.VarUint();
    const double serverTime = r.VarUint() / 1e6;
    const std::string text = r.String();
    if (r.Overflowed()) return;
    timeOffset_ = serverTime - now;
    haveOffset_ = true;
    if (onWelcome) onWelcome(text);
    else {
      scene.Clear();
      std::string err;
      if (!ReadSceneText(scene, text, nullptr, nullptr, &err)) LogError("net: the server's world could not be read: {}", err);
    }
    objectOf_.clear();
    for (SceneObject* o : AllObjects(scene))
      if (const NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType())) objectOf_[(uint32_t)no->netId] = o->id;
    joined_ = true;
    LogInfo("net: joined as player {} ({} networked objects)", localId_, objectOf_.size());
    StartOwned(scene, objectOf_, localId_, owned_);
  } else if (type == kSpawn) {
    const std::string snapshot = r.String();
    if (r.Overflowed()) return;
    Command c;
    c.name = "add";
    c.source = "net";
    c.args = "{\"snapshot\":" + snapshot + "}";
    std::string err;
    if (!bus.Execute(c, &err)) { LogWarn("net: spawn: {}", err); return; }
    ++count_.spawned;
    for (SceneObject* o : AllObjects(scene)) // the new object and its children
      if (const NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType()); no && !objectOf_.count((uint32_t)no->netId))
        objectOf_[(uint32_t)no->netId] = o->id;
    StartOwned(scene, objectOf_, localId_, owned_);
  } else if (type == kDespawn) {
    const uint32_t netId = (uint32_t)r.VarUint();
    auto it = objectOf_.find(netId);
    if (it == objectOf_.end()) return;
    if (scene.Find(it->second)) {
      Command c;
      c.name = "del";
      c.target = it->second;
      c.source = "net";
      bus.Execute(c, nullptr);
    }
    buffers_.erase(netId);
    owned_.erase(netId);
    objectOf_.erase(it);
    ++count_.despawned;
  } else if (type == kFields) {
    const uint32_t netId = (uint32_t)r.VarUint();
    const std::string comp = r.String(), json = r.String();
    auto it = objectOf_.find(netId);
    SceneObject* o = it == objectOf_.end() ? nullptr : scene.Find(it->second);
    Component* c = o ? o->Find(comp) : nullptr;
    if (!c || r.Overflowed()) return;
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(json.data(), json.size(), 0), yyjson_doc_free);
    yyjson_val* root = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    for (const FieldInfo& f : c->type->fields) {
      yyjson_val* v = yyjson_obj_get(root, f.name.c_str());
      if (!v || !(f.flags & kReplicated)) continue;
      char* text = yyjson_val_write(v, 0, nullptr);
      if (text) Registry::FromJson(static_cast<unsigned char*>(c->data) + f.offset, f.value, text, nullptr);
      std::free(text);
    }
    ++count_.fields;
  } else if (type == kSnapshot) {
    const uint32_t seq = (uint32_t)r.VarUint(), baseSeq = (uint32_t)r.VarUint();
    const double serverTime = r.VarUint() / 1e6; // microseconds
    const uint32_t count = (uint32_t)r.VarUint();
    const std::map<uint32_t, NetTransform>* base = nullptr;
    if (baseSeq) {
      auto b = received_.find(baseSeq);
      if (b == received_.end()) return; // the server deltas against one we no longer have: wait for the next
      base = &b->second;
    }
    std::map<uint32_t, NetTransform>& states = received_[seq];
    std::vector<std::pair<uint32_t, uint32_t>> acks; // net id -> move seq
    for (uint32_t k = 0; k < count && !r.Overflowed(); ++k) {
      const uint32_t netId = (uint32_t)r.VarUint();
      const bool hasMove = r.Bool();
      const uint32_t moveSeq = hasMove ? (uint32_t)r.VarUint() : 0;
      NetTransform b;
      if (base)
        if (auto f = base->find(netId); f != base->end()) b = f->second;
      states[netId] = net::ReadTransformDelta(r, b);
      if (hasMove) acks.push_back({netId, moveSeq});
    }
    if (r.Overflowed()) { received_.erase(seq); return; }
    while (received_.size() > 64) received_.erase(received_.begin());
    // tell the server what we have (deltas start from it); keep the clock in step
    BitWriter ack;
    ack.Bits(kAck, 8);
    ack.VarUint(seq);
    transport_.Send(1, Channel::Unreliable, ack.Data().data(), ack.Data().size());
    if ((int32_t)(seq - lastSeq_) <= 0) return; // older than what is shown: kept for deltas only
    lastSeq_ = seq;
    ++count_.snapshots;
    const double offset = serverTime - now;
    timeOffset_ = haveOffset_ ? std::max(offset, timeOffset_ + (offset - timeOffset_) * 0.05) : offset; // the fastest path wins
    haveOffset_ = true;
    for (auto& [netId, t] : states) {
      auto own = owned_.find(netId);
      bool reconciled = false;
      for (auto& [id, moveSeq] : acks)
        if (id == netId && own != owned_.end()) { // our own predicted object: correct it, replay what the server has not seen
          const float err = own->second.prediction.Reconcile(moveSeq, t.position);
          if (err > 0.01f) ++count_.corrections;
          if (auto o = objectOf_.find(netId); o != objectOf_.end())
            if (Transform* tr = Comp<Transform>(scene.Find(o->second), Transform::StaticType())) {
              const glm::vec3 p = own->second.prediction.Position();
              tr->position = {p.x, p.y, p.z};
              own->second.last = p;
            }
          reconciled = true;
        }
      if (!reconciled) buffers_[netId].Push(serverTime, t);
    }
  } else if (type == kPlayers) {
    const uint32_t n = (uint32_t)r.VarUint();
    std::vector<NetPlayer> list;
    for (uint32_t k = 0; k < n && !r.Overflowed(); ++k) {
      NetPlayer p;
      p.id = (uint32_t)r.VarUint();
      p.name = r.String();
      list.push_back(p);
    }
    if (!r.Overflowed()) players_ = std::move(list);
  } else if (type == kRpc) {
    const std::string object = r.String(), comp = r.String(), method = r.String(), args = r.String();
    r.Bits(2);
    if (!r.Overflowed()) DeliverRpc(object, comp, method, args, 0);
  }
}

// ---- sending ----
void NetWorld::Send(Scene& scene, CommandBus& bus, double now, double dt) {
  clock_ = now;
  if (!transport_.Active()) return;
  if (IsServer()) ServerSend(scene, bus, now, dt);
  else ClientSend(scene, bus, now, dt);
  std::vector<net::Event> none;
  transport_.Poll(now, none); // flush what was queued this frame (nothing is lost: events wait for Receive)
  for (const net::Event& e : none) {
    if (IsServer()) HandleServer(scene, bus, e, now);
    else HandleClient(scene, bus, e, now);
  }
}

void NetWorld::ServerSend(Scene& scene, CommandBus& bus, double now, double dt) {
  (void)bus;
  // 1. new networked objects: net ids first (children too), then one spawn per top-most new object
  std::vector<SceneObject*> all = AllObjects(scene);
  std::set<std::string> fresh;
  for (SceneObject* o : all)
    if (NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType()); no && !netIds_.count(o->id)) {
      no->netId = (int32_t)nextNetId_++;
      netIds_[o->id] = (uint32_t)no->netId;
      fresh.insert(o->id);
    }
  for (const std::string& id : fresh) {
    std::string parent = scene.Find(id)->parent;
    bool inside = false;
    for (; !parent.empty(); parent = scene.Find(parent) ? scene.Find(parent)->parent : std::string())
      if (fresh.count(parent)) inside = true;
    for (const Component& c : scene.Find(id)->components) fieldsSent_[id + "|" + c.type->name] = FieldsOf(scene, id, c.type->name);
    if (inside) continue; // comes with its parent
    BitWriter w;
    w.Bits(kSpawn, 8);
    w.String(scene.Snapshot(id));
    for (auto& [cid, c] : clients_)
      if (c->welcomed) transport_.Send(cid, Channel::Reliable, w.Data().data(), w.Data().size());
    ++count_.spawned;
  }
  // 2. gone ones
  for (auto it = netIds_.begin(); it != netIds_.end();) {
    if (scene.Find(it->first)) { ++it; continue; }
    BitWriter w;
    w.Bits(kDespawn, 8);
    w.VarUint(it->second);
    for (auto& [cid, c] : clients_)
      if (c->welcomed) transport_.Send(cid, Channel::Reliable, w.Data().data(), w.Data().size());
    for (auto f = fieldsSent_.begin(); f != fieldsSent_.end();) f = f->first.rfind(it->first + "|", 0) == 0 ? fieldsSent_.erase(f) : std::next(f);
    ++count_.despawned;
    it = netIds_.erase(it);
  }
  // 3. replicated fields that changed
  for (auto& [object, netId] : netIds_) {
    const SceneObject* o = scene.Find(object);
    for (const Component& c : o->components) {
      const std::string key = object + "|" + c.type->name, now2 = FieldsOf(scene, object, c.type->name);
      if (now2.empty() || fieldsSent_[key] == now2) continue;
      fieldsSent_[key] = now2;
      BitWriter w;
      w.Bits(kFields, 8);
      w.VarUint(netId);
      w.String(c.type->name);
      w.String(now2);
      for (auto& [cid, cl] : clients_)
        if (cl->welcomed) transport_.Send(cid, Channel::Reliable, w.Data().data(), w.Data().size());
      ++count_.fields;
    }
  }
  // 4. snapshots of the networked transforms, per client (delta against what it acknowledged, relevant objects only)
  sendAcc_ += dt;
  if (sendAcc_ < 1.0 / kSnapshotRate && dt > 0) return;
  sendAcc_ = std::fmod(sendAcc_, 1.0 / kSnapshotRate);
  ++snapshotSeq_;
  for (auto& [cid, c] : clients_) {
    if (!c->welcomed) continue;
    glm::vec3 eye(0);
    bool haveEye = false;
    for (auto& [object, netId] : netIds_)
      if (const NetworkObject* no = Comp<NetworkObject>(scene.Find(object), NetworkObject::StaticType()); no && (uint32_t)no->owner == cid)
        if (const Transform* t = Comp<Transform>(scene.Find(object), Transform::StaticType())) {
          eye = {t->position.x, t->position.y, t->position.z};
          haveEye = true;
          break;
        }
    const std::map<uint32_t, NetTransform>* base = nullptr;
    if (auto b = c->sent.find(c->ackSeq); c->ackSeq && b != c->sent.end()) base = &b->second;
    BitWriter w;
    w.Bits(kSnapshot, 8);
    w.VarUint(snapshotSeq_);
    w.VarUint(base ? c->ackSeq : 0);
    w.VarUint((uint64_t)std::llround(std::max(now, 0.0) * 1e6));
    std::vector<std::pair<uint32_t, NetTransform>> entries;
    std::map<uint32_t, NetTransform>& record = c->sent[snapshotSeq_];
    for (auto& [object, netId] : netIds_) {
      const SceneObject* o = scene.Find(object);
      const NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType());
      const Transform* t = Comp<Transform>(o, Transform::StaticType());
      if (!no || !t || !Comp<NetworkTransform>(o, NetworkTransform::StaticType())) continue;
      if (no->relevancy > 0 && haveEye && glm::length(glm::vec3(t->position.x, t->position.y, t->position.z) - eye) > no->relevancy && (uint32_t)no->owner != cid)
        continue;
      entries.push_back({netId, Read(*t)});
    }
    w.VarUint(entries.size());
    for (auto& [netId, t] : entries) {
      w.VarUint(netId);
      auto m = c->moveSeq.find(netId);
      w.Bool(m != c->moveSeq.end());
      if (m != c->moveSeq.end()) w.VarUint(m->second);
      NetTransform b;
      if (base)
        if (auto f = base->find(netId); f != base->end()) b = f->second;
      net::WriteTransformDelta(w, b, t);
      record[netId] = net::Quantized(t);
    }
    while (c->sent.size() > 64) c->sent.erase(c->sent.begin());
    transport_.Send(cid, Channel::Unreliable, w.Data().data(), w.Data().size());
  }
  ++count_.snapshots;
}

void NetWorld::ClientSend(Scene& scene, CommandBus&, double now, double) {
  if (!joined_) return;
  const double renderTime = now + timeOffset_ - settings_.interpolationDelay;
  for (auto& [netId, object] : objectOf_) {
    SceneObject* o = scene.Find(object);
    const NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType());
    const NetworkTransform* nt = Comp<NetworkTransform>(o, NetworkTransform::StaticType());
    Transform* tr = Comp<Transform>(o, Transform::StaticType());
    if (!no || !nt || !tr) continue;
    if ((uint32_t)no->owner == localId_ && nt->ownerPredicted) { // ours: what the scripts did this frame becomes a move
      Owned& own = owned_[netId];
      const glm::vec3 p(tr->position.x, tr->position.y, tr->position.z);
      if (!own.started) {
        own.started = true;
        own.prediction.Reset(p);
        own.last = p;
        continue;
      }
      const glm::vec3 delta = p - own.last;
      if (glm::length(delta) < 1e-5f) continue;
      const uint32_t seq = own.prediction.Add(delta);
      own.last = p;
      BitWriter w;
      w.Bits(kMove, 8);
      w.VarUint(netId);
      w.VarUint(seq);
      w.Float(delta.x);
      w.Float(delta.y);
      w.Float(delta.z);
      transport_.Send(1, Channel::Reliable, w.Data().data(), w.Data().size());
      ++count_.moves;
      continue;
    }
    auto b = buffers_.find(netId);
    if (b == buffers_.end() || b->second.Size() == 0) continue;
    Write(*tr, b->second.Sample(renderTime));
  }
}

// ---- players ----
void NetWorld::Welcome(Scene& scene, uint32_t client) {
  BitWriter w;
  w.Bits(kWelcome, 8);
  w.VarUint(client);
  w.VarUint((uint64_t)std::llround(std::max(clock_, 0.0) * 1e6));
  w.String(WriteSceneText(scene, "Net"));
  transport_.Send(client, Channel::Reliable, w.Data().data(), w.Data().size());
}

void NetWorld::SpawnPlayer(Scene& scene, CommandBus& bus, uint32_t player) {
  const GameMode* gm = nullptr;
  for (SceneObject* o : AllObjects(scene))
    if (const GameMode* g = Comp<GameMode>(o, GameMode::StaticType())) { gm = g; break; }
  if (!gm || gm->playerPrefab.empty()) return;
  // the starts: PlayerStart components (by order) or objects named PlayerStart*
  std::vector<std::pair<int, std::string>> starts;
  for (SceneObject* o : AllObjects(scene))
    if (const PlayerStart* ps = Comp<PlayerStart>(o, PlayerStart::StaticType())) starts.push_back({ps->order, o->id});
    else if (o->name.rfind("PlayerStart", 0) == 0) starts.push_back({1000, o->id});
  std::stable_sort(starts.begin(), starts.end(), [](auto& a, auto& b) { return a.first < b.first; });
  glm::vec3 at(0);
  if (!starts.empty()) at = glm::vec3(scene.WorldMatrix(starts[(players_.size()) % starts.size()].second)[3]);
  std::string source;
  if (!gm->playerPrefab.ends_with(".prefab"))
    for (SceneObject* o : AllObjects(scene))
      if (o->name == gm->playerPrefab) { source = o->id; break; }
  const std::string req = std::format(R"({{"op":"spawn","source":"{}","name":"{}","position":[{},{},{}]}})", source,
                                      source.empty() ? gm->playerPrefab : std::format("Player {}", player), at.x, at.y, at.z);
  const std::string answer = HandleScriptRequest(scene, bus, req);
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(answer.data(), answer.size(), 0), yyjson_doc_free);
  const char* id = doc ? yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc.get()), "value")) : nullptr;
  if (!id) { LogWarn("net: could not spawn the player: {}", answer); return; }
  SceneObject* o = scene.Find(id);
  if (o && !source.empty()) o->name = std::format("Player {}", player);
  for (SceneObject* d : AllObjects(scene)) { // the copy and its children belong to the player
    std::string p = d->id;
    while (!p.empty() && p != id) p = scene.Find(p) ? scene.Find(p)->parent : std::string();
    if (p == id)
      if (NetworkObject* no = Comp<NetworkObject>(d, NetworkObject::StaticType())) no->owner = (int32_t)player;
  }
  if (GameMode* g = const_cast<GameMode*>(gm)) g->players = (int32_t)players_.size() + 1;
}

void NetWorld::PlayerLeft(Scene& scene, CommandBus& bus, uint32_t player) {
  std::vector<std::string> gone;
  for (SceneObject* o : AllObjects(scene))
    if (const NetworkObject* no = Comp<NetworkObject>(o, NetworkObject::StaticType()); no && (uint32_t)no->owner == player && no->destroyWithOwner)
      gone.push_back(o->id);
  for (const std::string& id : gone)
    if (scene.Find(id)) {
      Command c;
      c.name = "del";
      c.target = id;
      c.source = "net";
      bus.Execute(c, nullptr);
    }
  std::erase_if(players_, [&](const NetPlayer& p) { return p.id == player; });
  for (SceneObject* o : AllObjects(scene))
    if (GameMode* g = Comp<GameMode>(o, GameMode::StaticType())) g->players = (int32_t)players_.size();
  BroadcastPlayers();
  if (onPlayer) onPlayer(player, false);
}

void NetWorld::BroadcastPlayers() {
  BitWriter w;
  w.Bits(kPlayers, 8);
  w.VarUint(players_.size());
  for (const NetPlayer& p : players_) {
    w.VarUint(p.id);
    w.String(p.name);
  }
  for (auto& [cid, c] : clients_)
    if (c->welcomed) transport_.Send(cid, Channel::Reliable, w.Data().data(), w.Data().size());
}

// ---- RPCs ----
void NetWorld::DeliverRpc(const std::string& object, const std::string& type, const std::string& method, const std::string& args, uint32_t sender) {
  ++count_.rpcs;
  if (onRpc) onRpc(object, type, method, args, sender);
}

void NetWorld::CallRpc(const Scene& scene, const std::string& object, const std::string& type, const std::string& method, const std::string& args,
                       RpcKind kind, bool reliable) {
  BitWriter w;
  w.Bits(kRpc, 8);
  w.String(object);
  w.String(type);
  w.String(method);
  w.String(args);
  w.Bits((uint32_t)kind, 2);
  const Channel ch = reliable ? Channel::Reliable : Channel::Unreliable;
  const bool networked = transport_.Active();
  if (kind == RpcKind::Server) {
    if (!networked || IsServer()) DeliverRpc(object, type, method, args, localId_);
    else transport_.Send(1, ch, w.Data().data(), w.Data().size());
    return;
  }
  if (networked && IsClient()) { // Client / Multicast calls belong to the server
    ++count_.rejected;
    LogWarn("net: {}.{}: a {} call runs on the server only", type, method, kind == RpcKind::Client ? "Client" : "Multicast");
    return;
  }
  if (kind == RpcKind::Client) {
    const NetworkObject* no = Comp<NetworkObject>(scene.Find(object), NetworkObject::StaticType());
    const uint32_t owner = no ? (uint32_t)no->owner : 0;
    if (owner == 0 || !networked) DeliverRpc(object, type, method, args, 0);
    else if (clients_.count(owner)) transport_.Send(owner, ch, w.Data().data(), w.Data().size());
    return;
  }
  DeliverRpc(object, type, method, args, 0); // multicast: here and everywhere
  if (networked)
    for (auto& [cid, c] : clients_)
      if (c->welcomed) transport_.Send(cid, ch, w.Data().data(), w.Data().size());
}

} // namespace bdg
