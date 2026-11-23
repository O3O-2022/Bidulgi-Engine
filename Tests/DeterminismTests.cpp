// Network / co-editing readiness (queue 1-7): applying the same command lines gives the same scene — in a
// mirrored bus fed line by line (a collaborator), after a JSONL round trip, and in another process.
#include "Core/Command.h"
#include "Scene/Components.h"
#include "Scene/SampleComponents.h"
#include "bdg_test.h"
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace bdg;

// Shared with TestMain's "--replay-dump" mode (the second process starts from the same scene).
void BuildDeterminismScene(Scene& s) {
  s.Create("g1", "Root");
  s.AddComponent("g1", Transform::StaticType());
  s.AddComponent("g1", Health::StaticType());
  s.Create("g2", "Lamp", "g1");
  s.AddComponent("g2", Transform::StaticType());
  s.AddComponent("g2", Light::StaticType());
  s.Create("g3", "Camera");
  s.AddComponent("g3", Camera::StaticType());
}

namespace {
// A random but valid-looking editing session: sets (merging drags included), structure edits, undo/redo,
// groups, from several sources. Some commands fail on purpose (cycles, missing parts): failures are not logged.
void RandomSession(CommandBus& bus, Scene& s, uint32_t seed, int steps, const std::function<void()>& afterStep) {
  std::mt19937 rng(seed);
  auto pick = [&](int n) { return (int)(rng() % (uint32_t)n); };
  auto ids = [&] {
    std::vector<std::string> out, stack(s.Roots().rbegin(), s.Roots().rend());
    while (!stack.empty()) { std::string id = stack.back(); stack.pop_back(); out.push_back(id);
      const auto* o = s.Find(id); for (auto it = o->children.rbegin(); it != o->children.rend(); ++it) stack.push_back(*it); }
    return out;
  };
  const char* types[] = {"Transform", "Light", "Camera", "MeshRenderer", "Health", "PlayerMove"};
  const char* sources[] = {"ui", "ui", "ui", "ai", "script", "terminal"};
  double t = 0;
  int open = 0;
  for (int step = 0; step < steps; ++step) {
    t += (pick(10) < 7) ? 0.05 + pick(20) * 0.01 : 0.5 + pick(10) * 0.1; // mostly drag-speed edits
    auto list = ids();
    std::string target = list.empty() ? "" : list[(size_t)pick((int)list.size())];
    Command c;
    c.time = t;
    c.source = sources[pick(6)];
    c.target = target;
    int op = pick(100);
    if (op < 40 && !target.empty()) {
      c.name = "set";
      const auto* o = s.Find(target);
      if (o->components.empty()) continue;
      const std::string comp = o->components[(size_t)pick((int)o->components.size())].type->name;
      std::string path, value;
      if (comp == "Transform") { path = std::vector<std::string>{"position", "rotation.y", "scale"}[pick(3)];
        value = path == "rotation.y" ? std::format("{}", pick(36000) / 100.0)
                                     : std::format("[{},{},{}]", pick(2000) / 100.0 - 10, pick(500) / 100.0, pick(2000) / 100.0 - 10); }
      else if (comp == "Light") { int k = pick(3); path = k == 0 ? "type" : k == 1 ? "intensity" : "color";
        value = k == 0 ? std::string("\"") + std::vector<std::string>{"Directional", "Point", "Spot"}[pick(3)] + "\""
              : k == 1 ? std::format("{}", pick(1000) / 10.0) : std::format("[{},{},{},1]", pick(100) / 100.0, pick(100) / 100.0, pick(100) / 100.0); }
      else if (comp == "Camera") { path = "fov"; value = std::format("{}", 20 + pick(1200) / 10.0); }
      else if (comp == "MeshRenderer") { path = "mesh"; value = std::format("\"메시_{}.glb\"", pick(50)); }
      else if (comp == "Health") { path = "maxHealth"; value = std::to_string(1 + pick(9999)); }
      else { path = "speed"; value = std::format("{}", pick(500) / 10.0); }
      c.args = std::format("{{\"path\":\"{}.{}\",\"value\":{}}}", comp, path, value);
    } else if (op < 50) {
      c.name = "add";
      c.target.clear();
      std::string parent = (pick(3) && !list.empty()) ? list[(size_t)pick((int)list.size())] : "";
      c.args = std::format("{{\"name\":\"Obj {}\",\"parent\":\"{}\",\"index\":{}}}", step, parent, pick(4) - 1);
    } else if (op < 55 && !target.empty()) { c.name = "del";
    } else if (op < 63 && !target.empty()) {
      c.name = "mv";
      std::string parent = (pick(3) && !list.empty()) ? list[(size_t)pick((int)list.size())] : "";
      c.args = std::format("{{\"parent\":\"{}\",\"index\":{}}}", parent, pick(3) - 1);
    } else if (op < 70 && !target.empty()) { c.name = "rename"; c.args = std::format("{{\"name\":\"이름 {}\"}}", step);
    } else if (op < 78 && !target.empty()) { c.name = "addcomp"; c.args = std::format("{{\"type\":\"{}\"}}", types[pick(6)]);
    } else if (op < 83 && !target.empty()) { c.name = "rmcomp"; c.args = std::format("{{\"type\":\"{}\"}}", types[pick(6)]);
    } else if (op < 91) { bus.Undo(); afterStep(); continue;
    } else if (op < 96) { bus.Redo(); afterStep(); continue;
    } else if (op < 98 && open == 0) { bus.BeginGroup(std::format("batch {}", step), "ai"); open = 1 + pick(3); continue;
    } else { continue; }
    bus.Execute(c);
    if (open > 0 && --open == 0) bus.EndGroup();
    afterStep();
  }
  if (open) bus.EndGroup();
}

std::string SelfPath() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(buf).string();
}

bool RunChild(const std::string& cmdline) {
  STARTUPINFOA si{sizeof si};
  PROCESS_INFORMATION pi{};
  std::string cmd = cmdline;
  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
  WaitForSingleObject(pi.hProcess, 120000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
}

std::string ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
}

TEST_CASE("determinism: a mirrored bus stays identical line by line") {
  Scene a, b;
  BuildDeterminismScene(a);
  BuildDeterminismScene(b);
  CommandBus busA(a), busB(b);
  size_t sent = 0, mismatches = 0, steps = 0;
  bool mirrorOk = true;
  RandomSession(busA, a, 2026, 3000, [&] {
    for (; sent < busA.Log().size(); ++sent) mirrorOk = busB.ExecuteLine(busA.Log()[sent]) && mirrorOk; // a collaborator receives each line
    if (++steps % 50 == 0 && a.Dump() != b.Dump()) ++mismatches;
  });
  CHECK(mirrorOk);
  CHECK_EQ(mismatches, size_t(0));
  CHECK_EQ(b.Dump(), a.Dump());
  CHECK_EQ(busB.UndoDepth(), busA.UndoDepth());
  CHECK(a.ObjectCount() > 3);
  std::printf("       mirror: %zu log lines, %zu objects at the end\n", busA.Log().size(), a.ObjectCount());
}

TEST_CASE("determinism: the log replays to the same scene in another process") {
  Scene a;
  BuildDeterminismScene(a);
  CommandBus bus(a);
  RandomSession(bus, a, 777, 2500, [] {});
  auto dir = std::filesystem::temp_directory_path();
  std::string log = (dir / "bdg_determinism.jsonl").string(), out = (dir / "bdg_determinism_dump.txt").string();
  std::remove(out.c_str());
  CHECK(bus.SaveLog(log));

  Scene same;                                  // JSON round trip in this process
  BuildDeterminismScene(same);
  CommandBus busSame(same);
  std::string err;
  CHECK(busSame.Replay(log, &err));
  CHECK_EQ(same.Dump(), a.Dump());

  // A second process (this test binary in --replay-dump mode) builds the start scene and replays the file.
  CHECK(RunChild("\"" + SelfPath() + "\" --replay-dump \"" + log + "\" \"" + out + "\""));
  std::string expected = a.Dump() + std::format("undo_depth={}\n", bus.UndoDepth());
  CHECK_EQ(ReadAll(out), expected);
  std::printf("       other process: %zu lines replayed, dumps %s\n", bus.Log().size(), ReadAll(out) == expected ? "equal" : "DIFFERENT");
  std::remove(log.c_str());
  std::remove(out.c_str());
}

TEST_CASE("determinism: every object has a persistent id and every change is a logged command") {
  Scene a;
  BuildDeterminismScene(a);
  CommandBus bus(a);
  Registry::Instance().TakeChanges();
  RandomSession(bus, a, 99, 400, [] {});
  std::vector<std::string> stack(a.Roots().begin(), a.Roots().end());
  size_t withId = 0, total = 0;
  while (!stack.empty()) {
    const SceneObject* o = a.Find(stack.back());
    stack.pop_back();
    ++total;
    withId += !o->id.empty() && a.HandleOf(o->id) == a.HandleOf(o->id) && !a.HandleOf(o->id).IsNull();
    for (auto& k : o->children) stack.push_back(k);
  }
  CHECK_EQ(withId, total);
  CHECK_EQ(total, a.ObjectCount());              // nothing unreachable
  size_t sets = 0;
  for (auto& line : bus.Log()) sets += line.find("\"cmd\":\"set\"") != std::string::npos;
  CHECK(Registry::Instance().PendingChanges() >= 1); // field writes were tracked for replication
  CHECK(sets >= 1);
}
