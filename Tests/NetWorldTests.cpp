// Multiplayer (round 7, M-3): a server and two clients in one process over loopback, with simulated loss.
#include "bdg_test.h"
#include "BTestComponent.h"
#include "Net/NetComponents.h"
#include "Net/NetWorld.h"
#include "Scene/Components.h"
#include "Scene/SceneFile.h"
#include "Scripting/ScriptApi.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <cstdio>
#include <thread>

using namespace bdg;

namespace {
struct Peer {
  Scene scene;
  CommandBus bus{scene};
  NetWorld net;
  std::vector<std::string> rpcs; // "Type.Method args object from sender"
};
Transform* Tr(Scene& s, const std::string& id) {
  SceneObject* o = s.Find(id);
  Component* c = o ? o->Find("Transform") : nullptr;
  return c ? static_cast<Transform*>(c->data) : nullptr;
}
std::string ByName(Scene& s, const std::string& name) {
  std::vector<std::string> stack(s.Roots().begin(), s.Roots().end());
  while (!stack.empty()) {
    SceneObject* o = s.Find(stack.back());
    stack.pop_back();
    if (!o) continue;
    if (o->name == name) return o->id;
    stack.insert(stack.end(), o->children.begin(), o->children.end());
  }
  return {};
}
int Owner(Scene& s, const std::string& id) {
  SceneObject* o = s.Find(id);
  Component* c = o ? o->Find("NetworkObject") : nullptr;
  return c ? static_cast<NetworkObject*>(c->data)->owner : -1;
}
bool Invincible(Scene& s, const char* id) {
  SceneObject* o = s.Find(id);
  Component* c = o ? o->Find("BTestComponent") : nullptr;
  return c && static_cast<bdgtest::BTestComponent*>(c->data)->invincible;
}
} // namespace

TEST_CASE("net world: server + 2 clients in one process with loss: spawn, sync, fields, prediction, late join, relevancy, RPCs, leave (round 7 M-3)") {
  const char* text = R"({"v":1,"scene":"Net"}
{"id":"gm","name":"Rules","GameMode":{"playerPrefab":"PlayerProto"}}
{"id":"ps1","name":"PlayerStart A","pos":[0,0,5],"PlayerStart":{"order":0}}
{"id":"ps2","name":"PlayerStart B","pos":[3,0,5],"PlayerStart":{"order":1}}
{"id":"proto","name":"PlayerProto","pos":[0,-100,0],"NetworkObject":{},"NetworkTransform":{"maxSpeed":10}}
{"id":"box","name":"Box","NetworkObject":{},"NetworkTransform":{},"BTestComponent":{}}
{"id":"far","name":"Far","pos":[500,0,0],"NetworkObject":{"relevancy":50},"NetworkTransform":{}}
{"id":"ground","name":"Ground"}
)";
  Peer s, a, b;
  std::string err;
  CHECK(ReadSceneText(s.scene, text, nullptr, nullptr, &err));
  NetSettings ss;
  ss.mode = NetMode::DedicatedServer;
  ss.port = 0;
  ss.address = "127.0.0.1";
  ss.sim.lossPercent = 10;
  CHECK(s.net.Start(s.scene, s.bus, ss, &err));
  NetSettings cs = ss;
  cs.mode = NetMode::Client;
  cs.port = s.net.Port();
  cs.playerName = "Ann";
  CHECK(a.net.Start(a.scene, a.bus, cs, &err));
  for (Peer* p : {&s, &a, &b})
    p->net.onRpc = [p](const std::string& obj, const std::string& type, const std::string& method, const std::string& args, uint32_t from) {
      p->rpcs.push_back(std::format("{}.{} {} {} from {}", type, method, args, obj, from));
    };
  const auto t0 = std::chrono::steady_clock::now();
  auto now = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
  auto frames = [&](int n, const std::function<void(int)>& each = {}) {
    for (int i = 0; i < n; ++i) {
      const double t = now();
      for (Peer* p : {&s, &a, &b}) p->net.Receive(p->scene, p->bus, t);
      if (each) each(i);
      for (Peer* p : {&s, &a, &b}) p->net.Send(p->scene, p->bus, t, 1.0 / 60);
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
  };
  for (int i = 0; i < 400 && !a.net.Joined(); ++i) frames(1);
  frames(20);
  CHECK(a.net.Joined() && a.net.LocalId() == 1);
  const std::string p1 = ByName(s.scene, "Player 1");
  CHECK(!p1.empty() && Owner(s.scene, p1) == 1 && a.scene.Find(p1) && Owner(a.scene, p1) == 1 && a.scene.Find("box") && a.scene.Find("ground"));
  CHECK(s.net.Players().size() == 1 && a.net.Players().size() == 1 && !a.net.Players().empty() && a.net.Players()[0].name == "Ann");
  CHECK(Tr(s.scene, p1) && std::fabs(Tr(s.scene, p1)->position.z - 5) < 1e-4f); // at the first PlayerStart

  // the server moves the box and sets a replicated field: the client follows (a little behind) and gets the value
  auto* flag = static_cast<bdgtest::BTestComponent*>(s.scene.Find("box")->Find("BTestComponent")->data);
  frames(60, [&](int i) {
    Tr(s.scene, "box")->position.x = i * 0.05f;
    if (i == 10) flag->invincible = true;
  });
  frames(30);
  CHECK(std::fabs(Tr(a.scene, "box")->position.x - 59 * 0.05f) < 0.05f && Invincible(a.scene, "box"));

  // the owner moves its player itself (prediction); the server follows within maxSpeed
  frames(30, [&](int) { Tr(a.scene, p1)->position.x += 0.05f; }); // 3 m/s
  frames(30);
  CHECK(std::fabs(Tr(s.scene, p1)->position.x - 1.5f) < 0.06f && std::fabs(Tr(a.scene, p1)->position.x - 1.5f) < 0.06f);
  // a cheating jump (50 m in one frame) is cut by the server and the owner is pulled back
  const uint64_t cut = s.net.Count().corrections;
  frames(1, [&](int) { Tr(a.scene, p1)->position.x += 50; });
  frames(60);
  CHECK(s.net.Count().corrections > cut && Tr(s.scene, p1)->position.x < 10 &&
        std::fabs(Tr(a.scene, p1)->position.x - Tr(s.scene, p1)->position.x) < 0.05f);

  // a late joiner gets everything as it is now, and its own player at the second start
  NetSettings cs2 = cs;
  cs2.playerName = "Bo";
  CHECK(b.net.Start(b.scene, b.bus, cs2, &err));
  for (int i = 0; i < 400 && !b.net.Joined(); ++i) frames(1);
  frames(20);
  const std::string p2 = ByName(s.scene, "Player 2");
  CHECK(b.net.Joined() && b.net.LocalId() == 2 && b.scene.Find(p1) && !p2.empty() && b.scene.Find(p2) && a.scene.Find(p2) && Owner(b.scene, p2) == 2);
  CHECK(std::fabs(Tr(b.scene, "box")->position.x - 59 * 0.05f) < 0.01f && Invincible(b.scene, "box"));
  CHECK(std::fabs(Tr(b.scene, p2)->position.x - 3) < 1e-3f && s.net.Players().size() == 2 && a.net.Players().size() == 2);
  // relevancy: "Far" (500 m away, relevancy 50) is not sent to either player
  frames(30, [&](int i) { Tr(s.scene, "far")->position.x = 500.f + i; });
  frames(10);
  CHECK(Tr(a.scene, "far")->position.x == 500 && Tr(b.scene, "far")->position.x == 500 && Tr(s.scene, "far")->position.x == 529);

  // spawned on the server: appears on both; destroyed: goes on both
  HandleScriptRequest(s.scene, s.bus, R"({"op":"spawn","source":"box","name":"Crate","position":[7,0,0]})");
  frames(40);
  const std::string crate = ByName(s.scene, "Crate");
  CHECK(!crate.empty() && a.scene.Find(crate) && b.scene.Find(crate) && Tr(b.scene, crate) && std::fabs(Tr(b.scene, crate)->position.x - 7) < 0.01f);
  HandleScriptRequest(s.scene, s.bus, std::format(R"({{"op":"destroy","target":"{}"}})", crate));
  frames(40);
  CHECK(!a.scene.Find(crate) && !b.scene.Find(crate));

  // RPCs: to the server (from the owner only), to the owner, to everyone
  a.net.CallRpc(a.scene, p1, "Gun", "Fire", "[1]", RpcKind::Server, true);
  b.net.CallRpc(b.scene, p1, "Gun", "Fire", "[2]", RpcKind::Server, true); // not its player: refused
  frames(40);
  s.net.CallRpc(s.scene, p1, "Gun", "Hit", "[]", RpcKind::Client, true);
  s.net.CallRpc(s.scene, "box", "Fx", "Boom", "[3]", RpcKind::Multicast, true);
  frames(40);
  auto has = [](const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); };
  CHECK(has(s.rpcs, std::format("Gun.Fire [1] {} from 1", p1)) && !has(s.rpcs, std::format("Gun.Fire [2] {} from 2", p1)) && s.net.Count().rejected >= 1);
  CHECK(has(a.rpcs, std::format("Gun.Hit [] {} from 0", p1)) && !has(b.rpcs, std::format("Gun.Hit [] {} from 0", p1)));
  CHECK(has(s.rpcs, "Fx.Boom [3] box from 0") && has(a.rpcs, "Fx.Boom [3] box from 0") && has(b.rpcs, "Fx.Boom [3] box from 0"));

  // a client with other content (a different build / pack hash) is turned away with the reason
  Peer m;
  NetSettings cm = cs;
  cm.content = {{"game", "0123456789abcdef"}};
  CHECK(m.net.Start(m.scene, m.bus, cm, &err));
  for (int i = 0; i < 200 && m.net.RejectReason().empty(); ++i) {
    frames(1);
    m.net.Receive(m.scene, m.bus, now());
    m.net.Send(m.scene, m.bus, now(), 1.0 / 60);
  }
  CHECK(!m.net.Joined() && m.net.RejectReason().find("different game content") != std::string::npos && s.net.Players().size() == 2);
  m.net.Stop();

  // the first player leaves: its player goes on the server and on the other client
  a.net.Stop();
  frames(60);
  CHECK(!s.scene.Find(p1) && !b.scene.Find(p1) && s.net.Players().size() == 1 && b.net.Players().size() == 1 && b.scene.Find(p2));
  CHECK(s.net.Count().spawned >= 3 && s.net.Transport().Stats(2).bytesSent > 0);
  b.net.Stop();
  s.net.Stop();
}
