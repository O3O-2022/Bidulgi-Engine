#include "Scripting/ScriptApi.h"
#include "Input/InputActions.h"
#include "Core/Log.h"
#include "Core/Reflection.h"
#include "Scripting/DataTable.h"
#include "Audio/Audio.h"
#include "Physics/PhysicsWorld.h"
#include "Project/ProjectSettings.h"
#include "Scripting/SaveGame.h"
#include "VFX/VfxWorld.h"
#include "Net/NetComponents.h"
#include "Net/NetWorld.h"
#include "Terrain/TerrainComponents.h"
#include "Terrain/TerrainData.h"
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <yyjson.h>
#include <cstdlib>
#include <format>
#include <functional>
#include <memory>

namespace bdg {
namespace {
std::string Q(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) o += std::format("\\u{:04x}", (unsigned)(unsigned char)c);
    else o += c;
  }
  return o + "\"";
}
std::string Ok(std::string_view valueJson = "null") { return std::format(R"({{"ok":true,"value":{}}})", valueJson); }
std::string Fail(std::string_view why) { return std::format(R"({{"ok":false,"error":{}}})", Q(why)); }

std::string Json(yyjson_val* v) {
  if (!v) return "null";
  size_t len = 0;
  char* s = yyjson_val_write(v, 0, &len);
  std::string r = s ? std::string(s, len) : "null";
  std::free(s);
  return r;
}
std::string Str(yyjson_val* obj, const char* key) {
  const char* s = yyjson_get_str(yyjson_obj_get(obj, key));
  return s ? s : "";
}

const SceneObject* FindByName(const Scene& scene, std::string_view name) {
  std::function<const SceneObject*(const std::vector<std::string>&)> walk = [&](const std::vector<std::string>& ids) -> const SceneObject* {
    for (const std::string& id : ids)
      if (const SceneObject* o = scene.Find(id)) {
        if (o->name == name) return o;
        if (const SceneObject* c = walk(o->children)) return c;
      }
    return nullptr;
  };
  return walk(scene.Roots());
}

// Ids of an object's children (deep: the whole subtree), in hierarchy order.
void ChildIds(const Scene& scene, const SceneObject& o, bool deep, std::vector<std::string>& out) {
  for (const std::string& id : o.children)
    if (const SceneObject* c = scene.Find(id)) {
      out.push_back(id);
      if (deep) ChildIds(scene, *c, deep, out);
    }
}

std::string IdList(const std::vector<std::string>& ids) {
  std::string s = "[";
  for (size_t i = 0; i < ids.size(); ++i) s += (i ? "," : "") + Q(ids[i]);
  return s + "]";
}

// A snapshot (Scene::Snapshot) made into a new copy: every id replaced by a fresh one, parent ids followed.
void Renumber(Scene& scene, yyjson_mut_doc* d, yyjson_mut_val* o, const std::string& parent) {
  const std::string id = scene.NewId();
  yyjson_mut_obj_put(o, yyjson_mut_strcpy(d, "id"), yyjson_mut_strcpy(d, id.c_str()));
  yyjson_mut_obj_put(o, yyjson_mut_strcpy(d, "parent"), yyjson_mut_strcpy(d, parent.c_str()));
  yyjson_mut_obj_put(o, yyjson_mut_strcpy(d, "prefab"), yyjson_mut_false(d));
  yyjson_mut_val* kids = yyjson_mut_obj_get(o, "children");
  size_t i, n;
  yyjson_mut_val* c;
  yyjson_mut_arr_foreach(kids, i, n, c) Renumber(scene, d, c, id);
}

// Sets Transform.<field> of a snapshot object to a JSON array value (adds a Transform when it has none).
void PutTransform(yyjson_mut_doc* d, yyjson_mut_val* o, const char* field, yyjson_val* value) {
  yyjson_mut_val* comps = yyjson_mut_obj_get(o, "components");
  size_t i, n;
  yyjson_mut_val* c;
  yyjson_mut_val* t = nullptr;
  yyjson_mut_arr_foreach(comps, i, n, c) {
    const char* type = yyjson_mut_get_str(yyjson_mut_obj_get(c, "type"));
    if (type && std::string_view(type) == "Transform") t = c;
  }
  if (!t) {
    t = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, t, "type", "Transform");
    yyjson_mut_obj_add_val(d, t, "value", yyjson_mut_obj(d));
    yyjson_mut_arr_insert(comps, t, 0);
  }
  yyjson_mut_val* v = yyjson_mut_obj_get(t, "value");
  if (!yyjson_mut_is_obj(v)) {
    v = yyjson_mut_obj(d);
    yyjson_mut_obj_put(t, yyjson_mut_strcpy(d, "value"), v);
  }
  yyjson_mut_obj_put(v, yyjson_mut_strcpy(d, field), yyjson_val_mut_copy(d, value));
}
} // namespace

std::string HandleScriptRequest(Scene& scene, CommandBus& bus, std::string_view requestJson) {
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(requestJson.data(), requestJson.size(), 0), yyjson_doc_free);
  yyjson_val* r = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
  if (!yyjson_is_obj(r)) return Fail("the request is not a JSON object");
  const std::string op = Str(r, "op"), target = Str(r, "target");
  auto run = [&](const char* name, std::string args) {
    Command c;
    c.name = name;
    c.target = target;
    c.args = std::move(args);
    c.source = "script";
    std::string err;
    return bus.Execute(c, &err) ? std::string() : (err.empty() ? std::string("command failed") : err);
  };

  if (op == "log") {
    const std::string level = Str(r, "level"), text = Str(r, "text");
    if (level == "error") LogError("{}", text);
    else if (level == "warn") LogWarn("{}", text);
    else LogInfo("{}", text);
    return Ok();
  }
  if (op == "find") {
    const std::string name = Str(r, "name");
    if (const SceneObject* o = scene.Find(name)) return Ok(Q(o->id));
    const SceneObject* o = FindByName(scene, name);
    return Ok(o ? Q(o->id) : "null");
  }
  if (op == "input") return Ok(InputSystem::Instance().StateJson(Str(r, "action"))); // 6-4: an action's state (null = unknown)
  if (op == "inputcontext") {
    const bool on = yyjson_get_bool(yyjson_obj_get(r, "on"));
    return InputSystem::Instance().EnableContext(Str(r, "name"), on) ? Ok() : Fail("no input context \"" + Str(r, "name") + "\"");
  }
  if (op.rfind("audio.", 0) == 0) { // the easy API's Audio (6-3): not undoable, not logged (game state)
    AudioSystem& audio = AudioSystem::Instance();
    if (!audio.Running()) return Fail("audio is not running (it starts with Play)");
    auto num = [&](const char* key, double def) {
      yyjson_val* v = yyjson_obj_get(r, key);
      return yyjson_is_num(v) ? yyjson_get_num(v) : def;
    };
    if (op == "audio.play") {
      AudioPlay p;
      p.clip = Str(r, "clip");
      if (const std::string g = Str(r, "group"); !g.empty()) p.group = g;
      p.volume = (float)num("volume", 1);
      p.pitch = (float)num("pitch", 1);
      p.loop = yyjson_get_bool(yyjson_obj_get(r, "loop"));
      if (yyjson_val* at = yyjson_obj_get(r, "at"); yyjson_is_arr(at) && yyjson_arr_size(at) == 3) {
        p.spatial = true;
        p.position = {(float)yyjson_get_num(yyjson_arr_get(at, 0)), (float)yyjson_get_num(yyjson_arr_get(at, 1)), (float)yyjson_get_num(yyjson_arr_get(at, 2))};
        p.minDistance = (float)num("minDistance", 1);
        p.maxDistance = (float)num("maxDistance", 50);
      }
      std::string err;
      const uint32_t id = audio.Play(p, &err);
      return id ? Ok(std::to_string(id)) : Fail(err);
    }
    const uint32_t id = (uint32_t)num("id", 0);
    if (op == "audio.stop") { audio.Stop(id); return Ok(); }
    if (op == "audio.playing") return Ok(audio.IsPlaying(id) ? "true" : "false");
    if (op == "audio.stopall") { audio.StopAll(); return Ok(); }
    if (op == "audio.volume") {
      const std::string group = Str(r, "group");
      if (yyjson_is_num(yyjson_obj_get(r, "volume")) && !audio.SetGroupVolume(group, (float)num("volume", 1)))
        return Fail(std::format("no mixer group \"{}\" (groups: Master, Music, Effects, Voice, UI, Ambient, ...)", group));
      const float v = audio.GroupVolume(group);
      return v < 0 ? Fail(std::format("no mixer group \"{}\"", group)) : Ok(std::format("{}", v));
    }
    return Fail(std::format("unknown op \"{}\"", op));
  }
  auto vec3 = [&](const char* key, glm::vec3& out) {
    yyjson_val* v = yyjson_obj_get(r, key);
    if (!yyjson_is_arr(v) || yyjson_arr_size(v) != 3) return false;
    out = {(float)yyjson_get_num(yyjson_arr_get(v, 0)), (float)yyjson_get_num(yyjson_arr_get(v, 1)), (float)yyjson_get_num(yyjson_arr_get(v, 2))};
    return true;
  };
  auto number = [&](const char* key, double def) {
    yyjson_val* v = yyjson_obj_get(r, key);
    return yyjson_is_num(v) ? yyjson_get_num(v) : def;
  };
  if (op.rfind("save.", 0) == 0) { // player saves (6-5b): the script holds the values, the engine the files
    ProjectSettings game;
    std::error_code ec;
    const std::filesystem::path settings = std::filesystem::current_path(ec) / "ProjectSettings" / "Project.json";
    if (std::filesystem::exists(settings, ec)) {
      const std::u8string u = settings.generic_u8string();
      LoadProjectSettings(std::string(u.begin(), u.end()), game);
    }
    const std::string folder = GameSaveFolder(game), slot = Str(r, "slot");
    if (op == "save.folder") return Ok(Q(folder));
    if (op == "save.list") return Ok(IdList(ListSaveSlots(folder)));
    if (op == "save.delete") return Ok(DeleteSaveSlot(folder, slot) ? "true" : "false");
    if (op == "save.read") {
      std::string data, err;
      if (ReadSaveSlot(folder, slot, data, &err)) return Ok(data);
      return err.empty() ? Ok("null") : Fail(err);
    }
    if (op == "save.write") {
      yyjson_val* data = yyjson_obj_get(r, "data");
      if (!yyjson_is_obj(data)) return Fail("save.write: data must be an object");
      std::string err;
      if (!WriteSaveSlot(folder, slot, Json(data), game, &err)) return Fail(err);
      return Ok(Q(SaveSlotPath(folder, slot)));
    }
    return Fail(std::format("unknown op \"{}\"", op));
  }
  if (op == "overlap") { // 6-5b: Physics.OverlapSphere through the running world
    const PhysicsWorld* world = PhysicsWorld::Current();
    if (!world) return Fail("physics is not running (it starts with Play)");
    glm::vec3 center;
    if (!vec3("center", center)) return Fail("overlap: center is [x, y, z]");
    std::vector<std::string> ids;
    for (const std::string& id : world->OverlapSphere(center, (float)number("radius", 1), yyjson_get_bool(yyjson_obj_get(r, "triggers"))))
      if (scene.Find(id)) ids.push_back(id);
    return Ok(IdList(ids));
  }
  if (op.rfind("net.", 0) == 0) { // multiplayer (round 7): state, RPCs, players, starting / leaving a session
    NetWorld* net = NetWorld::Game();
    if (op == "net.state") {
      const bool networked = net && net->Active();
      const std::string mode = net ? NetModeName(net->Mode()) : "Standalone";
      const bool server = !networked || net->IsServer(), client = networked && net->IsClient();
      bool owner = true, authority = server;
      const std::string t = Str(r, "target");
      if (!t.empty() && networked) owner = net->IsOwner(scene, t);
      return Ok(std::format(R"({{"mode":{},"server":{},"client":{},"local":{},"owner":{},"authority":{},"joined":{}}})", Q(mode), server ? "true" : "false",
                            client ? "true" : "false", net ? net->LocalId() : 0, owner ? "true" : "false", authority ? "true" : "false",
                            !networked || net->Joined() ? "true" : "false"));
    }
    if (op == "net.players") {
      std::string out = "[";
      if (net)
        for (const NetPlayer& p : net->Players()) out += std::format("{}{{\"id\":{},\"name\":{}}}", out.size() > 1 ? "," : "", p.id, Q(p.name));
      return Ok(out + "]");
    }
    if (op == "net.stats") {
      const net::PeerStats s = net && net->Active() ? net->Stats() : net::PeerStats{};
      return Ok(std::format(R"({{"ping":{},"loss":{},"send":{},"receive":{}}})", s.rttMs, s.lossPercent, s.sendKbps, s.recvKbps));
    }
    if (op == "net.rpc") {
      const std::string kind = Str(r, "kind");
      const RpcKind k = kind == "server" ? RpcKind::Server : kind == "client" ? RpcKind::Client : RpcKind::Multicast;
      if (!net) return Fail("net.rpc: the game is not running");
      net->CallRpc(scene, target, Str(r, "type"), Str(r, "method"), Json(yyjson_obj_get(r, "args")), k, !yyjson_is_bool(yyjson_obj_get(r, "reliable")) || yyjson_get_bool(yyjson_obj_get(r, "reliable")));
      return Ok();
    }
    if (op == "net.start" || op == "net.leave") {
      NetSettings s = net ? net->Settings() : NetSettings{};
      std::string err;
      if (op == "net.leave") s.mode = NetMode::Standalone;
      else {
        const std::string mode = Str(r, "mode");
        s.mode = mode == "listen" ? NetMode::ListenServer : mode == "server" ? NetMode::DedicatedServer : NetMode::Client;
        if (s.mode == NetMode::Client) s.address = Str(r, "address").empty() ? "127.0.0.1" : Str(r, "address");
        else s.address = "";
        s.port = (uint16_t)number("port", 7777);
        if (!Str(r, "name").empty()) s.playerName = Str(r, "name");
      }
      return NetWorld::RestartGame(s, &err) ? Ok() : Fail(err);
    }
    return Fail(std::format("unknown op \"{}\"", op));
  }
  if (op == "terrain.height") { // round 6: Terrain.HeightAt / NormalAt (world x, z): the first terrain that covers it
    const double x = number("x", 0), z = number("z", 0);
    std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
    while (!stack.empty()) {
      SceneObject* o = scene.Find(stack.back());
      stack.pop_back();
      if (!o) continue;
      stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
      Component* c = o->Find("Terrain");
      if (!c) continue;
      const std::string& assetPath = static_cast<const Terrain*>(c->data)->asset;
      TerrainData* t = TerrainRegistry::Instance().Get(assetPath);
      if (!t) continue;
      const glm::vec3 origin(scene.WorldMatrix(o->id)[3]);
      const float lx = (float)x - origin.x, lz = (float)z - origin.z;
      if (!t->Contains(lx, lz)) continue;
      const glm::vec3 n = t->NormalAt(lx, lz);
      return Ok(std::format(R"({{"id":{},"height":{},"normal":[{},{},{}]}})", Q(o->id), origin.y + t->HeightAt(lx, lz), n.x, n.y, n.z));
    }
    return Ok("null");
  }
  if (op.rfind("vfx.", 0) == 0) { // particle effects (6-VFX P-1), while playing
    VfxWorld& vfx = VfxWorld::Instance();
    if (!vfx.Active()) return Fail("effects play while the game runs (Play)");
    if (op == "vfx.play") {
      glm::vec3 at(0), euler(0);
      vec3("at", at);
      vec3("rotation", euler);
      std::string err;
      const uint32_t id = vfx.Play(Str(r, "effect"), at, glm::quat(glm::radians(euler)), (uint32_t)number("seed", 0), &err);
      return id ? Ok(std::to_string(id)) : Fail(err);
    }
    const uint32_t id = (uint32_t)number("id", 0);
    if (op == "vfx.stop") { vfx.Stop(id, yyjson_get_bool(yyjson_obj_get(r, "clear"))); return Ok(); }
    if (op == "vfx.playing") return Ok(vfx.Playing(id) ? "true" : "false");
    if (op == "vfx.alive") return Ok(std::to_string(yyjson_obj_get(r, "id") ? vfx.Alive(id) : vfx.Alive()));
    return Fail(std::format("unknown op \"{}\"", op));
  }
  if (op == "table") { // a data table, whole (the script caches it until play stops)
    DataTable t;
    std::string err;
    std::vector<std::string> problems;
    if (!LoadDataTable(Str(r, "name"), t, &err, &problems)) return Fail(err);
    for (const std::string& p : problems) LogWarn("data table: {}", p);
    return Ok(DataTableJson(t));
  }
  if (op == "raycast") { // 7-1b / 6-5b: Physics.Raycast(origin, direction, maxDistance, triggers, ignore) through the running world
    auto vec = [&](const char* key) {
      yyjson_val* a = yyjson_obj_get(r, key);
      glm::vec3 v(0.f);
      for (size_t i = 0; i < 3 && yyjson_is_arr(a) && i < yyjson_arr_size(a); ++i) v[(int)i] = (float)yyjson_get_num(yyjson_arr_get(a, i));
      return v;
    };
    std::vector<std::string> ignore;
    size_t i, n;
    yyjson_val* v;
    yyjson_arr_foreach(yyjson_obj_get(r, "ignore"), i, n, v)
      if (const char* s = yyjson_get_str(v)) ignore.push_back(s);
    const double maxDist = yyjson_is_num(yyjson_obj_get(r, "max")) ? yyjson_get_num(yyjson_obj_get(r, "max")) : 1000.0;
    const PhysicsWorld* world = PhysicsWorld::Current();
    PhysicsWorld::RayResult hit;
    const bool triggers = yyjson_get_bool(yyjson_obj_get(r, "triggers")); // 6-5b
    if (!world || !world->Raycast(vec("origin"), vec("dir"), (float)maxDist, hit, ignore, triggers) || !scene.Find(hit.object)) return Ok("null");
    return Ok(std::format(R"({{"id":{},"point":[{},{},{}],"normal":[{},{},{}],"distance":{}}})", Q(hit.object), hit.point.x, hit.point.y, hit.point.z,
                          hit.normal.x, hit.normal.y, hit.normal.z, hit.distance));
  }
  if (op == "findall") { // by name ("" = all), hierarchy order, with world positions (FindAll / FindNearest)
    const std::string name = Str(r, "name");
    std::string out = "[";
    std::vector<std::string> stack(scene.Roots().rbegin(), scene.Roots().rend());
    while (!stack.empty()) {
      const SceneObject* o = scene.Find(stack.back());
      stack.pop_back();
      if (!o) continue;
      stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
      if (!name.empty() && o->name != name) continue;
      const Vec3 p = scene.WorldPosition(o->id);
      out += std::format("{}{{\"id\":{},\"name\":{},\"pos\":[{},{},{}]}}", out.size() > 1 ? "," : "", Q(o->id), Q(o->name), p.x, p.y, p.z);
    }
    return Ok(out + "]");
  }
  if (op == "spawn") { // a copy of `source` (with its children) or a new empty object, as one "add" command
    const std::string source = Str(r, "source"), parent = Str(r, "parent"), name = Str(r, "name");
    if (!parent.empty() && !scene.Find(parent)) return Fail(std::format("spawn: no parent object \"{}\"", parent));
    if (source.empty() && (name.ends_with(".prefab") || name.starts_with("guid:"))) { // Spawn("Prefabs/Enemy.prefab"): an instance (6-1)
      const std::string id = scene.NewId();
      Command c;
      c.name = "addprefab";
      c.args = std::format(R"({{"prefab":{},"id":{},"parent":{}}})", Q(name), Q(id), Q(parent));
      c.source = "script";
      std::string err;
      if (!bus.Execute(c, &err)) return Fail("spawn: " + err);
      for (const char* f : {"position", "rotation", "scale"})
        if (yyjson_val* v = yyjson_obj_get(r, f); yyjson_is_arr(v)) {
          Command s;
          s.name = "set";
          s.target = id;
          s.source = "script";
          s.args = std::format(R"({{"path":"Transform.{}","value":{}}})", f, Json(v));
          bus.Execute(s, nullptr);
        }
      return Ok(Q(id));
    }
    std::string snap;
    if (!source.empty()) {
      if (!scene.Find(source)) return Fail(std::format("spawn: no object \"{}\" to copy", source));
      snap = scene.Snapshot(source);
    } else {
      snap = std::format(R"({{"id":"","name":{},"parent":"","index":-1,"prefab":false,"components":[{{"type":"Transform","value":{{}}}}],"children":[]}})",
                         Q(name.empty() ? "GameObject" : name));
    }
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> in(yyjson_read(snap.data(), snap.size(), 0), yyjson_doc_free);
    std::unique_ptr<yyjson_mut_doc, decltype(&yyjson_mut_doc_free)> d(yyjson_doc_mut_copy(in.get(), nullptr), yyjson_mut_doc_free);
    yyjson_mut_val* root = d ? yyjson_mut_doc_get_root(d.get()) : nullptr;
    if (!root) return Fail("spawn: could not copy the object");
    Renumber(scene, d.get(), root, parent);
    yyjson_mut_obj_put(root, yyjson_mut_strcpy(d.get(), "index"), yyjson_mut_sint(d.get(), -1));
    if (!name.empty()) yyjson_mut_obj_put(root, yyjson_mut_strcpy(d.get(), "name"), yyjson_mut_strcpy(d.get(), name.c_str()));
    for (const char* f : {"position", "rotation", "scale"})
      if (yyjson_val* v = yyjson_obj_get(r, f); yyjson_is_arr(v)) PutTransform(d.get(), root, f, v);
    const std::string id = yyjson_mut_get_str(yyjson_mut_obj_get(root, "id"));
    char* text = yyjson_mut_val_write(root, 0, nullptr);
    const std::string json = text ? text : "";
    std::free(text);
    Command c;
    c.name = "add";
    c.args = "{\"snapshot\":" + json + "}";
    c.source = "script";
    std::string err;
    if (!bus.Execute(c, &err)) return Fail("spawn: " + (err.empty() ? std::string("could not create the object") : err));
    return Ok(Q(id));
  }
  if (op != "name" && op != "rename" && op != "children" && op != "destroy" && op != "has" && op != "get" && op != "set" && op != "call")
    return Fail(std::format("unknown op \"{}\"", op));
  SceneObject* o = scene.Find(target);
  if (!o) return Fail(std::format("no object \"{}\"", target));
  if (op == "name") return Ok(Q(o->name));
  if (op == "rename") {
    const std::string err = run("rename", std::format(R"({{"name":{}}})", Q(Str(r, "name"))));
    return err.empty() ? Ok() : Fail(err);
  }
  if (op == "children") {
    std::vector<std::string> ids;
    ChildIds(scene, *o, yyjson_get_bool(yyjson_obj_get(r, "deep")), ids);
    return Ok(IdList(ids));
  }
  if (op == "destroy") { // answers the ids that went away, so the script ends their behaviours right away
    std::vector<std::string> ids{o->id};
    ChildIds(scene, *o, true, ids);
    const std::string err = run("del", "{}");
    return err.empty() ? Ok(IdList(ids)) : Fail(err);
  }
  if (op == "has") return Ok(o->Find(Str(r, "type")) ? "true" : "false");
  if (op == "get" || op == "set") {
    const std::string path = Str(r, "path");
    const size_t dot = path.find('.');
    Component* c = dot == std::string::npos ? nullptr : o->Find(std::string_view(path).substr(0, dot));
    if (!c) return Fail(std::format("\"{}\" has no component for \"{}\"", o->name, path));
    if (op == "get") {
      std::string v, err;
      if (!Registry::Get(c->data, *c->type, path.substr(dot + 1), v, &err)) return Fail(err);
      return Ok(v);
    }
    const std::string err = run("set", std::format(R"({{"path":{},"value":{}}})", Q(path), Json(yyjson_obj_get(r, "value"))));
    return err.empty() ? Ok() : Fail(err);
  }
  if (op == "call") {
    const std::string type = Str(r, "type"), function = Str(r, "function");
    Component* c = o->Find(type);
    if (!c) return Fail(std::format("\"{}\" has no component \"{}\"", o->name, type));
    const FunctionInfo* fn = c->type->FindFunction(function);
    if (!fn || !(fn->flags & (kCallable | kPure | kButton))) return Fail(std::format("{} has no callable function \"{}\"", type, function));
    yyjson_val* args = yyjson_obj_get(r, "args");
    std::vector<std::string> list;
    if (yyjson_is_arr(args)) {
      yyjson_arr_iter it = yyjson_arr_iter_with(args);
      for (yyjson_val* a = yyjson_arr_iter_next(&it); a; a = yyjson_arr_iter_next(&it)) list.push_back(Json(a));
    }
    if (list.size() != fn->params.size())
      return Fail(std::format("{}.{} takes {} argument(s), got {}", type, function, fn->params.size(), list.size()));
    if (fn->flags & kPure) { // a value: read only, no command
      std::string v, err;
      if (!Registry::Call(c->data, *c->type, function, list, &v, &err)) return Fail(err);
      return Ok(v.empty() ? "null" : v);
    }
    std::string joined;
    for (const std::string& a : list) joined += (joined.empty() ? "" : ",") + a;
    const std::string err = run("call", std::format(R"({{"type":{},"function":{},"args":[{}]}})", Q(type), Q(function), joined));
    if (!err.empty()) return Fail(err);
    return Ok(bus.LastResult().empty() ? "null" : bus.LastResult());
  }
  return Fail("unreachable");
}

} // namespace bdg
