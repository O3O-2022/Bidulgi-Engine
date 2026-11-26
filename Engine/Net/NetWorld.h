#pragma once
// The networked game (round 7, M-3): one per running game (play mode, the player exe; tests make several). The server
// owns the truth; clients show it.
//
// Joining: a client says hello; the server answers with its id and the whole current world as scene text (so a late
// joiner gets everything spawned so far), then keeps it up to date:
//  - Spawn / Despawn (reliable): objects with a NetworkObject that appear / go on the server.
//  - Fields (reliable): Replicated fields of a NetworkObject's components when they change.
//  - Snapshot (unreliable, NetworkTransform.sendRate): transforms delta-coded against the last snapshot the client
//    acknowledged, plus how far the server got with that client's own moves; objects beyond NetworkObject.relevancy
//    of the client's player are left out.
//  - Players (reliable): the player list.
//  - Rpc (either): a call to a method of a Behaviour on an object, routed by kind (to the server / to the owner / to all).
// The owner's client sends its predicted moves (reliable); the server applies them within maxSpeed.
//
// Per frame: Receive (before the scripts: apply what arrived) and Send (after them: client interpolation and moves,
// server spawns, fields, snapshots).
#include "Core/Command.h"
#include "Net/NetCodec.h"
#include "Net/Transport.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bdg {

enum class NetMode { Standalone, ListenServer, DedicatedServer, Client };
const char* NetModeName(NetMode m); // "Standalone", "Listen Server", ...

// What a game is made of, for the join check (mods later: Docs/QUEUE.md "게임 모드 지원"): the game build / content
// pack and, later, each mod, by name and hash. A client whose list differs from the server's is turned away with the
// reason. Assets and prefabs always travel by path or GUID (scene snapshots), never by an index that added content
// would shift.
struct ContentEntry {
  std::string name, hash;
};

struct NetSettings {
  NetMode mode = NetMode::Standalone;
  std::string address = "127.0.0.1"; // client: the server; server: the interface to listen on ("" = all)
  uint16_t port = 7777;              // server: 0 = any free port
  int maxClients = 8;
  std::string playerName = "Player";
  float interpolationDelay = 0.1f;   // seconds clients draw others in the past
  std::vector<ContentEntry> content; // compared on join (empty on both = anything goes)
  net::SimSettings sim;
  uint32_t timeoutMs = 5000;         // no answer this long = the other side is gone (tests on slow machines: longer)
};

struct NetPlayer {
  uint32_t id = 0;   // 0 = the host of a listen server
  std::string name;
};

enum class RpcKind { Server, Client, Multicast };

class NetWorld {
public:
  NetWorld();
  ~NetWorld();
  static NetWorld* Current(); // the game's (scripts); the last one started that is still running
  // The running game's world even while standalone (play mode sets it), and how scripts start / leave a session
  // (C# Net.Host / Join / Leave): the game restarts its NetWorld with these settings.
  static NetWorld* Game();
  static void SetGame(NetWorld* world, std::function<bool(const NetSettings&, std::string*)> restart);
  static bool RestartGame(const NetSettings& settings, std::string* error);

  bool Start(Scene& scene, CommandBus& bus, const NetSettings& settings, std::string* error = nullptr);
  void Stop();
  void Receive(Scene& scene, CommandBus& bus, double now);
  void Send(Scene& scene, CommandBus& bus, double now, double dt);

  NetMode Mode() const { return settings_.mode; }
  bool Active() const { return transport_.Active(); }
  bool IsServer() const { return settings_.mode == NetMode::ListenServer || settings_.mode == NetMode::DedicatedServer; }
  bool IsClient() const { return settings_.mode == NetMode::Client; }
  bool Joined() const { return IsServer() || joined_; } // client: the world has arrived
  uint32_t LocalId() const { return localId_; }        // server: 0
  uint16_t Port() const { return transport_.Port(); }
  bool IsOwner(const Scene& scene, const std::string& object) const;
  bool HasAuthority(const Scene& scene, const std::string& object) const; // the server (or nothing networked)
  const std::vector<NetPlayer>& Players() const { return players_; }
  net::PeerStats Stats() const; // client: to the server; server: the worst client
  net::Transport& Transport() { return transport_; }
  const NetSettings& Settings() const { return settings_; }
  NetSettings& MutableSettings() { return settings_; }

  // Calls `method` of the Behaviour `type` on `object` where the kind says (validated on the server: a Server call
  // must come from the object's owner). argsJson: a JSON array.
  void CallRpc(const Scene& scene, const std::string& object, const std::string& type, const std::string& method, const std::string& argsJson,
               RpcKind kind, bool reliable);
  std::function<void(const std::string& object, const std::string& type, const std::string& method, const std::string& argsJson, uint32_t sender)> onRpc;
  std::function<void(uint32_t player, bool joined)> onPlayer;      // server: a player joined / left (after the spawn)
  std::function<void(const std::string& sceneText)> onWelcome;      // client: the world arrived (default: replaces the scene)
  std::function<void(const std::string& why)> onDisconnected;       // client: the server went away

  struct Counters { uint64_t spawned = 0, despawned = 0, fields = 0, snapshots = 0, moves = 0, corrections = 0, rpcs = 0, rejected = 0; };
  const Counters& Count() const { return count_; }
  const std::string& RejectReason() const { return rejectReason_; } // client: why the server turned us away
  std::string Describe() const; // "Listen Server, 2 clients, ping 34 ms, loss 1 %, 12 / 30 kbit/s"

private:
  struct Client;
  struct Tracked { std::string object; uint32_t netId = 0; };
  void HandleServer(Scene& scene, CommandBus& bus, const net::Event& e, double now);
  void HandleClient(Scene& scene, CommandBus& bus, const net::Event& e, double now);
  void ServerSend(Scene& scene, CommandBus& bus, double now, double dt);
  void ClientSend(Scene& scene, CommandBus& bus, double now, double dt);
  void Welcome(Scene& scene, uint32_t client);
  void SpawnPlayer(Scene& scene, CommandBus& bus, uint32_t player);
  void PlayerLeft(Scene& scene, CommandBus& bus, uint32_t player);
  void BroadcastPlayers();
  std::string FieldsOf(const Scene& scene, const std::string& object, const std::string& type) const;
  void DeliverRpc(const std::string& object, const std::string& type, const std::string& method, const std::string& args, uint32_t sender);

  NetSettings settings_;
  net::Transport transport_;
  uint32_t localId_ = 0;
  bool joined_ = false, helloSent_ = false;
  std::string rejectReason_;
  std::vector<NetPlayer> players_;
  Counters count_;
  // server
  std::map<uint32_t, std::unique_ptr<Client>> clients_;
  std::map<std::string, uint32_t> netIds_;          // object id -> net id
  std::map<std::string, std::string> fieldsSent_;   // "object|type" -> replicated fields as last sent
  uint32_t nextNetId_ = 1;
  int spawnCount_ = 0; // players spawned: which PlayerStart is next
  uint32_t snapshotSeq_ = 0;
  double sendAcc_ = 0;
  std::vector<uint32_t> pendingJoins_;
  // client
  std::map<uint32_t, std::string> objectOf_;        // net id -> object id
  std::map<uint32_t, net::SnapshotBuffer> buffers_; // net id -> received transforms
  std::map<uint32_t, std::map<uint32_t, net::NetTransform>> received_; // snapshot seq -> states (for deltas)
  uint32_t lastSeq_ = 0;
  double timeOffset_ = 0;
  bool haveOffset_ = false;
  struct Owned { net::Prediction prediction; glm::vec3 last{0}; bool started = false; double sentAt = -1e9; };
  std::map<uint32_t, Owned> owned_;
  double clock_ = 0;
};

} // namespace bdg
