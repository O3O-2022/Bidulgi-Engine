// C# scripting (queue 4-1): bindings from reflection, the request channel and the hosted .NET runtime.
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Scripting/CSharpBindings.h"
#include "Scripting/DotNetHost.h"
#include "Scripting/ScriptApi.h"
#include "bdg_test.h"
#include <cstdio>

using namespace bdg;

TEST_CASE("C# bindings: components, fields, functions and enums from reflection (queue 4-1)") {
  const std::string cs = GenerateCSharpBindings();
  CHECK(cs.find("public sealed class Health : Component {") != std::string::npos);
  CHECK(cs.find("public int MaxHealth { get => Get<int>(\"maxHealth\"); set => Set(\"maxHealth\", value); }") != std::string::npos);
  CHECK(cs.find("public int Damage(int amount) => Call<int>(\"Damage\", amount);") != std::string::npos);
  CHECK(cs.find("public float Ratio() => Call<float>(\"Ratio\");") != std::string::npos);
  CHECK(cs.find("public void Revive() => CallVoid(\"Revive\");") != std::string::npos);
  CHECK(cs.find("public Vector3 Position {") != std::string::npos);                 // Transform.position
  CHECK(cs.find("public enum LightType { Directional = 0, Point = 1, Spot = 2 }") != std::string::npos);
  CHECK(cs.find("public LightType Type {") != std::string::npos);
  CHECK(cs == GenerateCSharpBindings()); // deterministic (the build rewrites it only when it changes)
}

TEST_CASE("script requests: find, get, set, call through the command bus (queue 4-1)") {
  Scene s;
  s.Create("g1", "Hero", "");
  s.AddComponent("g1", Health::StaticType());
  s.AddComponent("g1", Transform::StaticType());
  CommandBus bus(s);
  auto req = [&](const char* json) { return HandleScriptRequest(s, bus, json); };
  CHECK(req(R"({"op":"find","name":"Hero"})") == R"({"ok":true,"value":"g1"})");
  CHECK(req(R"({"op":"find","name":"g1"})") == R"({"ok":true,"value":"g1"})");
  CHECK(req(R"({"op":"find","name":"Nobody"})") == R"({"ok":true,"value":null})");
  CHECK(req(R"({"op":"has","target":"g1","type":"Health"})") == R"({"ok":true,"value":true})");
  CHECK(req(R"({"op":"get","target":"g1","path":"Health.current"})") == R"({"ok":true,"value":100})");
  CHECK(req(R"({"op":"call","target":"g1","type":"Health","function":"Damage","args":[30]})") == R"({"ok":true,"value":70})");
  CHECK(bus.UndoDepth() == 1 && bus.History().back().source == "script");
  CHECK(req(R"({"op":"call","target":"g1","type":"Health","function":"Ratio","args":[]})").find("0.7") != std::string::npos);
  CHECK(bus.UndoDepth() == 1); // Pure: no undo step
  CHECK(req(R"({"op":"set","target":"g1","path":"Transform.position","value":[1,2,3]})") == R"({"ok":true,"value":null})");
  CHECK(s.WorldPosition("g1").y == 2.f && bus.UndoDepth() == 2);
  CHECK(bus.Undo() && bus.Undo());
  CHECK(static_cast<Health*>(s.Find("g1")->Find("Health")->data)->current == 100);
  // errors are answers, not crashes
  CHECK(req("not json").find(R"("ok":false)") != std::string::npos);
  CHECK(req(R"({"op":"get","target":"g9","path":"Health.current"})").find("no object") != std::string::npos);
  CHECK(req(R"({"op":"call","target":"g1","type":"Health","function":"Damage","args":[]})").find("takes 1 argument") != std::string::npos);
  CHECK(req(R"({"op":"call","target":"g1","type":"Health","function":"Nope","args":[]})").find("no callable") != std::string::npos);
  CHECK(req(R"({"op":"fly"})").find("unknown op") != std::string::npos);
}

TEST_CASE("C# scripting: the .NET runtime runs C# that reads, calls and edits components (queue 4-1)") {
#if !BDG_HAS_DOTNET
  std::printf("  (skipped: built without the .NET SDK or with AddressSanitizer)\n");
#else
  DotNetHost& host = DotNetHost::Instance();
  std::string err;
  const bool started = host.Start({}, &err);
  CHECK(started);
  if (!started) return;
  Scene s;
  s.Create("g1", "Hero", "");
  Health& h = *static_cast<Health*>(s.AddComponent("g1", Health::StaticType()));
  h.current = 60;
  s.AddComponent("g1", Transform::StaticType());
  CommandBus bus(s);
  host.Attach(&s, &bus);
  CHECK(host.SelfTest("Hero") == 1);
  CHECK(h.current == 50 && h.maxHealth == 250 && s.WorldPosition("g1").z == 3.f);
  CHECK(bus.UndoDepth() == 3); // Damage, maxHealth, position: each one undo step from "script"
  while (bus.CanUndo()) bus.Undo();
  CHECK(h.current == 60 && h.maxHealth == 100);
  host.Attach(nullptr, nullptr);
  CHECK(host.SelfTest("Hero") == -1 || host.SelfTest("Hero") == 0); // no scene: C# gets an error, the engine lives
#endif
}
