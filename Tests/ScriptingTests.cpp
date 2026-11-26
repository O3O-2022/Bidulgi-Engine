// C# scripting (queue 4-1): bindings from reflection, the request channel and the hosted .NET runtime.
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "Core/Log.h"
#include "Scene/SceneFile.h"
#include "Scripting/BehaviourRunner.h"
#include "Scripting/CSharpBindings.h"
#include "Scripting/DataTable.h"
#include "Scripting/DotNetHost.h"
#include "Scripting/ScriptApi.h"
#include "bdg_test.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace bdg;

TEST_CASE("C# bindings: components, fields, functions and enums from reflection (queue 4-1)") {
  const std::string cs = GenerateCSharpBindings();
  CHECK(cs.find("public sealed partial class Health : Component {") != std::string::npos);
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
  // in play mode (a layer) script changes are game state: applied, but no undo step and no log line (4-2)
  bus.PushLayer();
  const size_t logLines = bus.Log().size();
  CHECK(req(R"({"op":"set","target":"g1","path":"Health.current","value":5})") == R"({"ok":true,"value":null})");
  CHECK(req(R"({"op":"call","target":"g1","type":"Health","function":"Damage","args":[1]})") == R"({"ok":true,"value":4})");
  CHECK(!bus.CanUndo() && bus.Log().size() == logLines);
  CHECK(bus.PopLayer());
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

TEST_CASE("C# behaviours: lifecycle order, enable/disable, destroy, fixed steps, errors (queue 4-2)") {
#if !BDG_HAS_DOTNET
  std::printf("  (skipped: built without the .NET SDK or with AddressSanitizer)\n");
#else
  DotNetHost& host = DotNetHost::Instance();
  std::string err;
  const int kSampleBehaviours = 15; // the Behaviour classes in Scripting/Bidulgi.Samples (PhysicsProbe 6-2, InputProbe 6-4, audio sample 6-3, PrefabSpawner 6-1, AnimProbe 7-1, GameApiProbe 6-5b, TerrainProbe round 6, NetProbe round 7)
  CHECK(host.Start({}, &err));
  CHECK(host.LoadScripts(DotNetHost::DefaultManagedDir() + "/Bidulgi.Samples.dll", &err) == kSampleBehaviours);
  const TypeInfo* spinner = Registry::Instance().Find("Spinner");
  CHECK(spinner && host.IsBehaviourType(spinner) && !host.IsBehaviourType(&Health::StaticType()));
  CHECK(spinner && std::find(spinner->tags.begin(), spinner->tags.end(), "CSharp") != spinner->tags.end());
  CHECK(host.LoadScripts(DotNetHost::DefaultManagedDir() + "/Bidulgi.Samples.dll", &err) == kSampleBehaviours); // again: same types
  CHECK(host.BehaviourTypeCount() == (size_t)kSampleBehaviours);

  std::vector<std::string> lines;
  SetLogHook([&](LogLevel, std::string_view m) { lines.emplace_back(m); });
  Scene s;
  s.Create("a", "Alpha", "");
  s.AddComponent("a", Transform::StaticType());
  Health& h = *static_cast<Health*>(s.AddComponent("a", Health::StaticType()));
  h.current = 10;
  for (const char* t : {"Spinner", "LifecycleProbe", "Medic"}) CHECK(s.AddComponent("a", *Registry::Instance().Find(t)) != nullptr);
  s.Create("b", "Beta", "");
  for (const char* t : {"Faulty", "LifecycleProbe"}) CHECK(s.AddComponent("b", *Registry::Instance().Find(t)) != nullptr);
  // scenes keep them by class name, like any component
  const std::string text = WriteSceneText(s, "T");
  CHECK(text.find("\"Spinner\":{}") != std::string::npos);
  Scene back;
  CHECK(ReadSceneText(back, text, nullptr, nullptr, &err) && back.Find("a")->Find("LifecycleProbe") != nullptr);

  CommandBus bus(s);
  BehaviourRunner r;
  r.Begin(s, bus);
  CHECK(r.Active() && r.Instances() == 5);
  for (int i = 0; i < 30; ++i) r.Frame(s, bus, 1.0 / 60.0);
  const Transform& t = *static_cast<Transform*>(s.Find("a")->Find("Transform")->data);
  CHECK(std::abs(t.rotation.y - 45.f) < 0.5f); // 90 degrees/s for half a second
  CHECK(h.current == 100);                     // Medic.Start healed through the Health binding
  size_t faultyErrors = 0;
  for (const std::string& l : lines) faultyErrors += l.find("Faulty.Update") != std::string::npos;
  CHECK(faultyErrors == 10); // 30 failing frames, 10 shown; the others kept running
  CHECK(std::count(lines.begin(), lines.end(), std::string("medic: found the spinner")) == 1);
  // enabled false -> OnDisable, no updates; true -> OnEnable again
  Command off{"set", "a", R"({"path":"LifecycleProbe.enabled","value":false})"};
  CHECK(bus.Execute(off, &err));
  r.Frame(s, bus, 1.0 / 60.0);
  Command on{"set", "a", R"({"path":"LifecycleProbe.enabled","value":true})"};
  CHECK(bus.Execute(on, &err));
  r.Frame(s, bus, 1.0 / 30.0);
  CHECK(r.FixedStepsLastFrame() == 2);
  r.Frame(s, bus, 1.0); // a very slow frame: at most 4 fixed steps
  CHECK(r.FixedStepsLastFrame() == 4);
  // removing the object destroys its behaviours on the next frame
  Command del{"del", "b", "{}"};
  CHECK(bus.Execute(del, &err));
  r.Frame(s, bus, 1.0 / 60.0);
  CHECK(r.Instances() == 3);
  auto probe = [&](const char* id) {
    for (const std::string& l : lines)
      if (l.rfind(std::string("probe: ") + id + " ", 0) == 0) return l;
    return std::string();
  };
  CHECK(probe("b").find("awake=1 enable=1 start=1") != std::string::npos && probe("b").find("order=ok") != std::string::npos);
  r.End();
  CHECK(r.Instances() == 0 && !r.Active());
  const std::string a = probe("a");
  CHECK(a.find("awake=1 enable=2 start=1") != std::string::npos);
  CHECK(a.find("disable=2 destroy=1 order=ok") != std::string::npos);
  CHECK(a.find("events=Awake,OnEnable,Start,Update,OnDisable,OnEnable,Update,OnDisable,OnDestroy") != std::string::npos);
  SetLogHook({});
  host.Attach(nullptr, nullptr);
#endif
}

TEST_CASE("script requests: children, findall, spawn, destroy for the easy API (queue 4-4c)") {
  Scene s;
  s.Create("g1", "Hero", "");
  s.AddComponent("g1", Transform::StaticType());
  s.Create("g2", "Sword", "g1");
  s.AddComponent("g2", Health::StaticType());
  s.Create("g3", "Gem", "g2");
  CommandBus bus(s);
  auto req = [&](const char* json) { return HandleScriptRequest(s, bus, json); };
  CHECK(req(R"({"op":"children","target":"g1"})") == R"({"ok":true,"value":["g2"]})");
  CHECK(req(R"({"op":"children","target":"g1","deep":true})") == R"({"ok":true,"value":["g2","g3"]})");
  CHECK(req(R"({"op":"findall","name":"Sword"})").find(R"("id":"g2")") != std::string::npos);
  CHECK(req(R"({"op":"findall","name":"Nobody"})") == R"({"ok":true,"value":[]})");
  // a copy of g1 with its children: new ids, at a new place, one undo step
  const std::string spawned = req(R"({"op":"spawn","source":"g1","name":"Hero 2","position":[4,5,6]})");
  CHECK(spawned.find(R"("ok":true)") != std::string::npos);
  const size_t q = spawned.find("\"value\":\"") + 9;
  const std::string id = spawned.substr(q, spawned.find('"', q) - q);
  const SceneObject* copy = s.Find(id);
  CHECK(copy && copy->name == "Hero 2" && copy->parent.empty() && copy->children.size() == 1);
  CHECK(copy && copy->children[0] != "g2" && s.Find(copy->children[0]) && s.Find(copy->children[0])->Find("Health"));
  CHECK(s.WorldPosition(id).z == 6.f && bus.UndoDepth() == 1);
  CHECK(s.Find("g2")->children.size() == 1); // the original is untouched
  // an empty object
  CHECK(req(R"({"op":"spawn","name":"Marker","position":[0,1,0]})").find(R"("ok":true)") != std::string::npos);
  CHECK(req(R"({"op":"findall","name":"Marker"})").find(R"("pos":[0,1,0])") != std::string::npos);
  // destroy answers the ids that went away, and undo brings them back
  CHECK(req(R"({"op":"destroy","target":"g2"})") == R"({"ok":true,"value":["g2","g3"]})");
  CHECK(!s.Find("g2") && !s.Find("g3"));
  CHECK(bus.Undo() && s.Find("g3"));
  CHECK(bus.Undo() && bus.Undo() && !s.Find(id));
  CHECK(req(R"({"op":"spawn","source":"g99"})").find("no object") != std::string::npos);
  CHECK(req(R"({"op":"destroy","target":"g99"})").find("no object") != std::string::npos);
}

TEST_CASE("data tables: .table.jsonl rows, header types, problems, CSV import / export (queue 4-4d)") {
  DataTable t;
  std::vector<std::string> problems;
  CHECK(ParseDataTable("{\"id\":\"rifle\",\"damage\":12}\n\n{\"id\":\"pistol\",\"damage\":5,\"range\":2.5}\n", t, &problems));
  CHECK(problems.empty() && t.key == "id" && t.rows.size() == 2 && t.keys[1] == "pistol");
  CHECK(t.columns.size() == 3 && t.columns[1].name == "damage" && t.columns[1].type == "int" && t.columns[2].type == "float");
  CHECK(t.Row("rifle") && t.Row("rifle")->find("12") != std::string::npos && !t.Row("laser"));
  // a header: key column and types are checked; bad rows are skipped with line numbers
  const char* text = R"({"$table":{"key":"name","columns":[{"name":"name","type":"string"},{"name":"hp","type":"int"}]}}
{"name":"goblin","hp":30}
{"name":"orc","hp":"lots"}
{"hp":3}
{"name":"goblin","hp":31}
not json
{"name":"bat","hp":2,"wings":2}
)";
  problems.clear();
  CHECK(ParseDataTable(text, t, &problems));
  CHECK(t.hasHeader && t.key == "name" && t.rows.size() == 2 && t.keys[0] == "goblin" && t.keys[1] == "bat");
  CHECK(problems.size() == 5);
  CHECK(!problems.empty() && problems[0].find("line 3") != std::string::npos && problems[0].find("should be int") != std::string::npos);
  // written with a header, read back the same
  DataTable back;
  CHECK(ParseDataTable(WriteDataTable(t), back) && back.rows == t.rows && back.key == "name" && back.columns.size() == 2);
  // CSV: quotes, escaped quotes, commas and line breaks inside cells, CRLF, BOM, inferred types, empty cells
  DataTable c;
  std::string err;
  CHECK(ImportCsv("\xEF\xBB\xBFid,hp,speed,boss,note\r\ngoblin,30,1.5,false,\"says \"\"hi\"\", loud\"\r\nghost,,2,TRUE,\"two\nlines\"\r\n", c, &err));
  CHECK(c.key == "id" && c.rows.size() == 2);
  CHECK(c.columns.size() == 5 && c.columns[1].type == "int" && c.columns[2].type == "float" && c.columns[3].type == "bool" && c.columns[4].type == "string");
  // (raw strings with \" stay outside CHECK: MSVC cannot stringize them)
  const std::string goblin = R"({"id":"goblin","hp":30,"speed":1.5,"boss":false,"note":"says \"hi\", loud"})";
  const std::string ghost = R"({"id":"ghost","speed":2.0,"boss":true,"note":"two\nlines"})";
  CHECK(c.rows.size() == 2 && c.rows[0] == goblin);
  CHECK(c.rows.size() == 2 && c.rows[1] == ghost);
  DataTable again;
  CHECK(ImportCsv(ExportCsv(c), again) && again.rows == c.rows); // export -> import gives the same rows
  CHECK(!ImportCsv("id,hp\ngoblin,\"30\n", c, &err) && err.find("not closed") != std::string::npos);
  CHECK(!ImportCsv("id,id\na,b\n", c, &err) && err.find("twice") != std::string::npos);
  // loading by name from the data folders; the script request answers the rows
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "bdg_data_tables";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "Weapons.table.jsonl") << "{\"id\":\"rifle\",\"damage\":12}\n";
  std::ofstream(dir / "Enemies.csv") << "id,hp\ngoblin,30\n";
  const std::u8string u8 = dir.u8string();
  SetDataFolders({std::string(u8.begin(), u8.end())});
  DataTable w;
  CHECK(LoadDataTable("Weapons", w, &err) && w.rows.size() == 1);
  CHECK(LoadDataTable("Enemies", w, &err) && w.Row("goblin"));
  CHECK(!LoadDataTable("Nothing", w, &err) && err.find("no table") != std::string::npos);
  CHECK(!LoadDataTable("../secret", w, &err));
  Scene s;
  CommandBus bus(s);
  CHECK(HandleScriptRequest(s, bus, R"({"op":"table","name":"Weapons"})") == R"({"ok":true,"value":{"name":"Weapons","key":"id","rows":[{"id":"rifle","damage":12}]}})");
  CHECK(HandleScriptRequest(s, bus, R"({"op":"table","name":"Nothing"})").find(R"("ok":false)") != std::string::npos);
  SetDataFolders({});
  std::filesystem::remove_all(dir);
}
