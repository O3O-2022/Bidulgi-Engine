#include "VFX/VfxAsset.h"
#include "Core/Utf8Path.h"
#include <yyjson.h>
#include <algorithm>
#include <format>
#include <memory>

namespace bdg::vfx {
namespace {
struct KindInfo { const char* name; Kind kind; Stage stage; };
constexpr KindInfo kKinds[] = {
    {"SpawnRate", Kind::SpawnRate, Stage::EmitterUpdate},       {"SpawnBurst", Kind::SpawnBurst, Stage::EmitterUpdate},
    {"Lifetime", Kind::Lifetime, Stage::ParticleSpawn},         {"PositionPoint", Kind::PositionPoint, Stage::ParticleSpawn},
    {"PositionSphere", Kind::PositionSphere, Stage::ParticleSpawn}, {"PositionCone", Kind::PositionCone, Stage::ParticleSpawn},
    {"Velocity", Kind::Velocity, Stage::ParticleSpawn},         {"VelocityCone", Kind::VelocityCone, Stage::ParticleSpawn},
    {"Size", Kind::Size, Stage::ParticleSpawn},                 {"Color", Kind::Color, Stage::ParticleSpawn},
    {"Gravity", Kind::Gravity, Stage::ParticleUpdate},          {"Drag", Kind::Drag, Stage::ParticleUpdate},
    {"ColorOverLife", Kind::ColorOverLife, Stage::ParticleUpdate}, {"SizeOverLife", Kind::SizeOverLife, Stage::ParticleUpdate},
    {"Collision", Kind::Collision, Stage::ParticleUpdate},      {"Sprite", Kind::Sprite, Stage::Render},
};
const char* kStageNames[] = {"emitter-update", "particle-spawn", "particle-update", "render"};

float Num(yyjson_val* v, float def) { return yyjson_is_num(v) ? (float)yyjson_get_num(v) : def; }
bool IsRef(yyjson_val* v) { const char* s = yyjson_get_str(v); return s && s[0] == '@'; }

// A number or [min, max].
bool ReadRange(yyjson_val* v, Range& out) {
  if (yyjson_is_num(v)) { out.lo = out.hi = (float)yyjson_get_num(v); return true; }
  if (yyjson_is_arr(v) && yyjson_arr_size(v) == 2 && yyjson_is_num(yyjson_arr_get(v, 0)) && yyjson_is_num(yyjson_arr_get(v, 1))) {
    out.lo = (float)yyjson_get_num(yyjson_arr_get(v, 0));
    out.hi = (float)yyjson_get_num(yyjson_arr_get(v, 1));
    if (out.lo > out.hi) std::swap(out.lo, out.hi);
    return true;
  }
  return false;
}
template <int N>
bool ReadVec(yyjson_val* v, glm::vec<N, float>& out) {
  if (!yyjson_is_arr(v) || yyjson_arr_size(v) != (size_t)N) return false;
  for (int i = 0; i < N; ++i) {
    yyjson_val* e = yyjson_arr_get(v, i);
    if (!yyjson_is_num(e)) return false;
    out[i] = (float)yyjson_get_num(e);
  }
  return true;
}
} // namespace

const char* KindName(Kind k) {
  for (const KindInfo& i : kKinds)
    if (i.kind == k) return i.name;
  return "?";
}

float EffectDef::LongestLife() const {
  float longest = 0;
  for (const EmitterDef& e : emitters) {
    float life = 1;
    for (const Module& m : e.modules)
      if (m.kind == Kind::Lifetime) life = m.value.hi;
    longest = std::max(longest, life);
  }
  return longest;
}

glm::vec4 SampleGradient(const std::vector<std::pair<float, glm::vec4>>& g, float t) {
  if (g.empty()) return glm::vec4(1);
  if (t <= g.front().first) return g.front().second;
  for (size_t i = 1; i < g.size(); ++i)
    if (t <= g[i].first) {
      const float span = g[i].first - g[i - 1].first;
      return glm::mix(g[i - 1].second, g[i].second, span > 1e-6f ? (t - g[i - 1].first) / span : 1.f);
    }
  return g.back().second;
}

float SampleCurve(const std::vector<std::pair<float, float>>& c, float t) {
  if (c.empty()) return 1.f;
  if (t <= c.front().first) return c.front().second;
  for (size_t i = 1; i < c.size(); ++i)
    if (t <= c[i].first) {
      const float span = c[i].first - c[i - 1].first;
      return c[i - 1].second + (c[i].second - c[i - 1].second) * (span > 1e-6f ? (t - c[i - 1].first) / span : 1.f);
    }
  return c.back().second;
}

bool ParseEffect(std::string_view text, EffectDef& out, std::vector<std::string>* problems, std::string* error) {
  out = EffectDef{};
  auto problem = [&](int line, std::string what) {
    if (problems) problems->push_back(std::format("line {}: {}", line, what));
  };
  int lineNo = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1;
    ++lineNo;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    if (line.empty() || line.starts_with("//") || line.starts_with("#")) {
      if (end == text.size()) break;
      continue;
    }
    std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(line.data(), line.size(), 0), yyjson_doc_free);
    yyjson_val* o = doc ? yyjson_doc_get_root(doc.get()) : nullptr;
    if (!yyjson_is_obj(o)) {
      problem(lineNo, "not a JSON object");
      if (end == text.size()) break;
      continue;
    }
    if (const char* name = yyjson_get_str(yyjson_obj_get(o, "vfx"))) {
      out.name = name;
    } else if (const char* ename = yyjson_get_str(yyjson_obj_get(o, "emitter"))) {
      EmitterDef e;
      e.name = ename;
      e.max = std::clamp((int)Num(yyjson_obj_get(o, "max"), 100), 1, 100000);
      e.duration = std::max(Num(yyjson_obj_get(o, "duration"), 1), 0.01f);
      e.delay = std::max(Num(yyjson_obj_get(o, "delay"), 0), 0.f);
      if (yyjson_val* l = yyjson_obj_get(o, "loop"); yyjson_is_bool(l)) e.loop = yyjson_get_bool(l);
      if (const char* sp = yyjson_get_str(yyjson_obj_get(o, "space"))) e.local = std::string_view(sp) == "local";
      if (const char* sim = yyjson_get_str(yyjson_obj_get(o, "sim")); sim && std::string_view(sim) == "gpu")
        problem(lineNo, std::format("emitter {}: the GPU path comes later (P-3); simulated on the CPU", ename));
      out.emitters.push_back(std::move(e));
    } else if (const char* mod = yyjson_get_str(yyjson_obj_get(o, "module"))) {
      if (out.emitters.empty()) { problem(lineNo, std::format("module {} before any emitter", mod)); goto next; }
      {
        const KindInfo* info = nullptr;
        for (const KindInfo& k : kKinds)
          if (std::string_view(k.name) == mod) info = &k;
        if (!info) { problem(lineNo, std::format("unknown module \"{}\"", mod)); goto next; }
        if (const char* st = yyjson_get_str(yyjson_obj_get(o, "stage")); st && std::string_view(st) != kStageNames[(int)info->stage])
          problem(lineNo, std::format("{} belongs to the {} stage, not {}", mod, kStageNames[(int)info->stage], st));
        if (yyjson_val* en = yyjson_obj_get(o, "enabled"); yyjson_is_bool(en) && !yyjson_get_bool(en)) goto next;
        Module m;
        m.kind = info->kind;
        m.stage = info->stage;
        yyjson_val* value = yyjson_obj_get(o, "value");
        if (IsRef(value)) problem(lineNo, std::format("{}: system parameters (@...) come later; the default is used", mod));
        auto range = [&](const char* key, Range& r) {
          yyjson_val* v = yyjson_obj_get(o, key);
          if (v && !ReadRange(v, r)) problem(lineNo, std::format("{}.{}: a number or [min, max]", mod, key));
        };
        auto vec3 = [&](const char* key, glm::vec3& v) {
          yyjson_val* x = yyjson_obj_get(o, key);
          if (x && !ReadVec<3>(x, v)) problem(lineNo, std::format("{}.{}: [x, y, z]", mod, key));
        };
        switch (m.kind) {
          case Kind::SpawnRate: m.value = {10, 10}; range("rate", m.value); range("value", m.value); break;
          case Kind::SpawnBurst:
            m.count = std::max((int)Num(yyjson_obj_get(o, "count"), 10), 0);
            m.time = std::max(Num(yyjson_obj_get(o, "time"), 0), 0.f);
            break;
          case Kind::Lifetime: range("value", m.value); break;
          case Kind::PositionPoint: vec3("value", m.vec); vec3("offset", m.vec); break;
          case Kind::PositionSphere: m.value = {0.5f, 0.5f}; range("radius", m.value); m.surface = yyjson_get_bool(yyjson_obj_get(o, "surface")); break;
          case Kind::PositionCone:
          case Kind::VelocityCone:
            m.vec = {0, 1, 0};
            vec3("axis", m.vec);
            m.angle = std::clamp(Num(yyjson_obj_get(o, "angle"), 30), 0.f, 180.f);
            if (m.kind == Kind::PositionCone) { m.value = {1, 1}; range("length", m.value); }
            else range("speed", m.speed);
            if (glm::length(m.vec) < 1e-6f) m.vec = {0, 1, 0};
            m.vec = glm::normalize(m.vec);
            break;
          case Kind::Velocity: vec3("value", m.vec); vec3("random", m.spread); break;
          case Kind::Size: m.value = {0.1f, 0.1f}; range("value", m.value); break;
          case Kind::Color:
            if (yyjson_val* c = yyjson_obj_get(o, "value"); c && !ReadVec<4>(c, m.color)) {
              glm::vec3 rgb;
              if (ReadVec<3>(c, rgb)) m.color = glm::vec4(rgb, 1);
              else if (!IsRef(c)) problem(lineNo, "Color.value: [r, g, b, a]");
            }
            break;
          case Kind::Gravity: m.vec = {0, -9.81f, 0}; vec3("value", m.vec); break;
          case Kind::Drag: range("value", m.value); break;
          case Kind::ColorOverLife:
          case Kind::SizeOverLife: {
            yyjson_val* keys = yyjson_obj_get(o, m.kind == Kind::ColorOverLife ? "gradient" : "curve");
            size_t i, n;
            yyjson_val* k;
            yyjson_arr_foreach(keys, i, n, k) {
              const float t = Num(yyjson_arr_get(k, 0), -1);
              yyjson_val* v = yyjson_arr_get(k, 1);
              if (t < 0 || t > 1) { problem(lineNo, std::format("{}: key {} needs a life value 0..1 first", mod, i)); continue; }
              if (m.kind == Kind::ColorOverLife) {
                glm::vec4 c(1);
                glm::vec3 rgb;
                if (ReadVec<4>(v, c)) m.gradient.push_back({t, c});
                else if (ReadVec<3>(v, rgb)) m.gradient.push_back({t, glm::vec4(rgb, 1)});
                else problem(lineNo, std::format("{}: key {} colour [r, g, b, a]", mod, i));
              } else if (yyjson_is_num(v)) {
                m.curve.push_back({t, (float)yyjson_get_num(v)});
              } else {
                problem(lineNo, std::format("{}: key {} needs a number", mod, i));
              }
            }
            std::stable_sort(m.gradient.begin(), m.gradient.end(), [](auto& a, auto& b) { return a.first < b.first; });
            std::stable_sort(m.curve.begin(), m.curve.end(), [](auto& a, auto& b) { return a.first < b.first; });
            break;
          }
          case Kind::Collision:
            m.plane = Num(yyjson_obj_get(o, "plane"), 0);
            m.bounce = std::clamp(Num(yyjson_obj_get(o, "bounce"), 0.3f), 0.f, 1.f);
            m.friction = std::clamp(Num(yyjson_obj_get(o, "friction"), 0.2f), 0.f, 1.f);
            m.kill = yyjson_get_bool(yyjson_obj_get(o, "kill"));
            break;
          case Kind::Sprite: {
            const char* b = yyjson_get_str(yyjson_obj_get(o, "blend"));
            m.blend = b && std::string_view(b) == "additive" ? Blend::Additive : Blend::Alpha;
            m.intensity = std::max(Num(yyjson_obj_get(o, "intensity"), 1), 0.f);
            if (yyjson_obj_get(o, "material") || yyjson_obj_get(o, "flipbook"))
              problem(lineNo, "Sprite: materials and flipbooks come later; drawn as a soft round sprite");
            out.emitters.back().blend = m.blend;
            out.emitters.back().intensity = m.intensity;
            break;
          }
        }
        out.emitters.back().modules.push_back(std::move(m));
      }
    } else {
      problem(lineNo, "a line needs \"vfx\", \"emitter\" or \"module\"");
    }
  next:
    if (end == text.size()) break;
  }
  if (out.emitters.empty()) {
    if (error) *error = "no emitter in the effect";
    return false;
  }
  return true;
}

bool LoadEffectFile(const std::string& path, EffectDef& out, std::vector<std::string>* problems, std::string* error) {
  std::string text;
  if (!ReadTextU8(path, text)) {
    if (error) *error = "cannot open " + path;
    return false;
  }
  return ParseEffect(text, out, problems, error);
}

} // namespace bdg::vfx
