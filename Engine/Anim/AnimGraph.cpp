#include "Anim/AnimGraph.h"
#include "Core/Log.h"
#include <yyjson.h>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>

namespace bdg {
namespace {
std::string Trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) ++a;
  while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
  return std::string(s.substr(a, b - a));
}

// Splits on a two-character operator ("||", "&&") outside nothing (no parentheses in this little language).
std::vector<std::string> Split(std::string_view s, std::string_view op) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i + op.size() <= s.size();) {
    if (s.substr(i, op.size()) == op) {
      parts.push_back(Trim(s.substr(start, i - start)));
      i += op.size();
      start = i;
    } else ++i;
  }
  parts.push_back(Trim(s.substr(start)));
  return parts;
}

bool ParseAtom(std::string text, AnimGraphDef::Cond& c, std::string* error) {
  while (!text.empty() && text[0] == '!') {
    c.negate = !c.negate;
    text = Trim(text.substr(1));
  }
  static const struct { const char* s; AnimGraphDef::Cond::Op op; } kOps[] = {
      {">=", AnimGraphDef::Cond::Ge}, {"<=", AnimGraphDef::Cond::Le}, {"==", AnimGraphDef::Cond::Eq}, {"!=", AnimGraphDef::Cond::Ne},
      {">", AnimGraphDef::Cond::Gt},  {"<", AnimGraphDef::Cond::Lt}};
  for (const auto& o : kOps) {
    const size_t at = text.find(o.s);
    if (at == std::string::npos) continue;
    c.var = Trim(text.substr(0, at));
    const std::string rhs = Trim(text.substr(at + std::strlen(o.s)));
    c.op = o.op;
    if (rhs == "true") c.value = 1;
    else if (rhs == "false") c.value = 0;
    else {
      char* end = nullptr;
      c.value = std::strtof(rhs.c_str(), &end);
      if (rhs.empty() || !end || *end) {
        if (error) *error = std::format("'{}': the right side must be a number, true or false", text);
        return false;
      }
    }
    break;
  }
  if (c.var.empty()) c.var = text; // bare name: bool / trigger
  for (char ch : c.var)
    if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '.')) {
      if (error) *error = std::format("'{}' is not a variable name", c.var);
      return false;
    }
  if (c.var.empty()) {
    if (error) *error = "empty condition";
    return false;
  }
  return true;
}

std::string Quote(std::string_view s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (c == '\n') { o += "\\n"; continue; }
    o += c;
  }
  return o + "\"";
}

std::string Num(float v) {
  std::string s = std::format("{}", v);
  return s;
}

const char* TypeName(AnimGraphDef::VarType t) {
  switch (t) {
    case AnimGraphDef::VarType::Int: return "int";
    case AnimGraphDef::VarType::Bool: return "bool";
    case AnimGraphDef::VarType::Trigger: return "trigger";
    default: return "float";
  }
}
}

int AnimGraphDef::Machine::FindState(std::string_view stateName) const {
  for (size_t i = 0; i < states.size(); ++i)
    if (states[i].name == stateName) return (int)i;
  return -1;
}

const AnimGraphDef::Var* AnimGraphDef::FindVar(std::string_view name) const {
  for (const Var& v : vars)
    if (v.name == name) return &v;
  return nullptr;
}

int AnimGraphDef::FindMachine(std::string_view name) const {
  for (size_t i = 0; i < machines.size(); ++i)
    if (machines[i].name == name) return (int)i;
  return -1;
}

bool ParseAnimCondition(std::string_view text, std::vector<std::vector<AnimGraphDef::Cond>>& out, std::string* error) {
  out.clear();
  if (Trim(text).empty()) return true;
  for (const std::string& orPart : Split(text, "||")) {
    std::vector<AnimGraphDef::Cond> group;
    for (const std::string& andPart : Split(orPart, "&&")) {
      AnimGraphDef::Cond c;
      if (!ParseAtom(andPart, c, error)) return false;
      group.push_back(std::move(c));
    }
    out.push_back(std::move(group));
  }
  return true;
}

void BlendSpaceWeights(const std::vector<AnimGraphDef::BlendSample>& s, bool is2D, float x, float y, std::vector<float>& w) {
  const size_t n = s.size();
  w.assign(n, 0.f);
  if (n == 0) return;
  if (n == 1) { w[0] = 1; return; }
  if (!is2D) { // the two neighbours along x
    size_t lo = 0, hi = 0;
    for (size_t i = 0; i < n; ++i) {
      if (s[i].x < s[lo].x) lo = i;
      if (s[i].x > s[hi].x) hi = i;
    }
    if (x <= s[lo].x) { w[lo] = 1; return; }
    if (x >= s[hi].x) { w[hi] = 1; return; }
    size_t a = lo, b = hi; // largest x <= value, smallest x >= value
    for (size_t i = 0; i < n; ++i) {
      if (s[i].x <= x && s[i].x >= s[a].x) a = i;
      if (s[i].x >= x && s[i].x <= s[b].x) b = i;
    }
    if (a == b || s[b].x == s[a].x) { w[a] = 1; return; }
    const float t = (x - s[a].x) / (s[b].x - s[a].x);
    w[a] = 1 - t;
    w[b] += t;
    return;
  }
  // gradient band interpolation (freeform cartesian)
  float sum = 0;
  for (size_t i = 0; i < n; ++i) {
    float wi = 1.f;
    for (size_t j = 0; j < n && wi > 0; ++j) {
      if (j == i) continue;
      const float ex = s[j].x - s[i].x, ey = s[j].y - s[i].y, len2 = ex * ex + ey * ey;
      if (len2 <= 1e-12f) continue;
      const float h = 1.f - ((x - s[i].x) * ex + (y - s[i].y) * ey) / len2;
      wi = std::min(wi, std::max(h, 0.f));
    }
    w[i] = wi;
    sum += wi;
  }
  if (sum > 1e-6f) {
    for (float& v : w) v /= sum;
    return;
  }
  size_t best = 0; // outside every band: the nearest sample
  float bd = 1e30f;
  for (size_t i = 0; i < n; ++i) {
    const float d = (x - s[i].x) * (x - s[i].x) + (y - s[i].y) * (y - s[i].y);
    if (d < bd) { bd = d; best = i; }
  }
  w[best] = 1;
}

namespace {
template <class Fail>
bool ParseEvents(yyjson_val* v, AnimGraphDef::State& st, Fail&& fail) {
  yyjson_val* evs = yyjson_obj_get(v, "events");
  if (!yyjson_is_arr(evs)) return true;
  size_t ei, en;
  yyjson_val* e;
  yyjson_arr_foreach(evs, ei, en, e) {
    AnimGraphDef::Event ev;
    if (const char* nm = yyjson_get_str(yyjson_obj_get(e, "event"))) ev.name = nm;
    ev.at = (float)yyjson_get_num(yyjson_obj_get(e, "at"));
    if (ev.name.empty()) return fail(std::format("state '{}': an event without a name", st.name));
    st.events.push_back(std::move(ev));
  }
  return true;
}
}

bool ParseAnimGraph(std::string_view json, AnimGraphDef& out, std::string* error) {
  out = {};
  auto fail = [&](std::string msg) {
    if (error) *error = std::move(msg);
    return false;
  };
  yyjson_doc* doc = yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_COMMENTS | YYJSON_READ_ALLOW_TRAILING_COMMAS);
  yyjson_val* root = doc ? yyjson_doc_get_root(doc) : nullptr;
  struct Free { yyjson_doc* d; ~Free() { yyjson_doc_free(d); } } guard{doc};
  if (!yyjson_is_obj(root)) return fail("not a JSON object");
  if (const char* s = yyjson_get_str(yyjson_obj_get(root, "skeleton"))) out.skeleton = s;
  if (yyjson_val* vars = yyjson_obj_get(root, "vars"); yyjson_is_obj(vars)) {
    size_t i, n;
    yyjson_val *k, *v;
    yyjson_obj_foreach(vars, i, n, k, v) {
      AnimGraphDef::Var var;
      var.name = yyjson_get_str(k);
      const std::string t = yyjson_get_str(v) ? yyjson_get_str(v) : "float";
      if (t == "float") var.type = AnimGraphDef::VarType::Float;
      else if (t == "int") var.type = AnimGraphDef::VarType::Int;
      else if (t == "bool") var.type = AnimGraphDef::VarType::Bool;
      else if (t == "trigger") var.type = AnimGraphDef::VarType::Trigger;
      else return fail(std::format("variable '{}': unknown type '{}' (float, int, bool, trigger)", var.name, t));
      out.vars.push_back(std::move(var));
    }
  }
  yyjson_val* machines = yyjson_obj_get(root, "stateMachines");
  if (!yyjson_is_obj(machines) || yyjson_obj_size(machines) == 0) return fail("no stateMachines");
  size_t mi, mn;
  yyjson_val *mk, *sm;
  yyjson_obj_foreach(machines, mi, mn, mk, sm) {
    AnimGraphDef::Machine m;
    m.name = yyjson_get_str(mk);
    if (!yyjson_is_obj(sm)) return fail(std::format("{}: the state machine is not an object", m.name));
    yyjson_val* states = yyjson_obj_get(sm, "states");
    if (!yyjson_is_obj(states) || yyjson_obj_size(states) == 0) return fail(std::format("{}: no states", m.name));
    size_t i, n;
    yyjson_val *k, *v;
    yyjson_obj_foreach(states, i, n, k, v) {
      AnimGraphDef::State st;
      st.name = yyjson_get_str(k);
      if (const char* p = yyjson_get_str(yyjson_obj_get(v, "play"))) st.clip = p;
      if (yyjson_val* b = yyjson_obj_get(v, "loop"); yyjson_is_bool(b)) st.loop = yyjson_get_bool(b);
      if (yyjson_val* s = yyjson_obj_get(v, "speed"); yyjson_is_num(s)) st.speed = (float)yyjson_get_num(s);
      if (yyjson_val* bs = yyjson_obj_get(v, "blendSpace")) {
        if (!yyjson_is_obj(bs)) return fail(std::format("state '{}': blend space files come later; write the blend space inline", st.name));
        if (const char* x = yyjson_get_str(yyjson_obj_get(bs, "x"))) st.blendX = x;
        if (const char* y = yyjson_get_str(yyjson_obj_get(bs, "y"))) st.blendY = y;
        if (st.blendX.empty()) return fail(std::format("state '{}': the blend space needs \"x\" (a variable)", st.name));
        for (const std::string& var : {st.blendX, st.blendY})
          if (!var.empty() && !out.FindVar(var)) return fail(std::format("state '{}': unknown variable '{}'", st.name, var));
        size_t si, sn;
        yyjson_val* s;
        yyjson_arr_foreach(yyjson_obj_get(bs, "samples"), si, sn, s) {
          AnimGraphDef::BlendSample b;
          if (const char* c = yyjson_get_str(yyjson_obj_get(s, "clip"))) b.clip = c;
          b.x = (float)yyjson_get_num(yyjson_obj_get(s, "x"));
          b.y = (float)yyjson_get_num(yyjson_obj_get(s, "y"));
          if (b.clip.empty()) return fail(std::format("state '{}': a blend sample without a clip", st.name));
          st.samples.push_back(std::move(b));
        }
        if (st.samples.empty()) return fail(std::format("state '{}': the blend space has no samples", st.name));
      }
      if (!ParseEvents(v, st, fail)) return false;
      m.states.push_back(std::move(st));
    }
    if (const char* e = yyjson_get_str(yyjson_obj_get(sm, "entry"))) {
      m.entry = m.FindState(e);
      if (m.entry < 0) return fail(std::format("{}: entry state '{}' does not exist", m.name, e));
    }
    if (yyjson_val* tr = yyjson_obj_get(sm, "transitions"); yyjson_is_arr(tr)) {
      size_t ti, tn;
      yyjson_val* tv;
      yyjson_arr_foreach(tr, ti, tn, tv) {
        AnimGraphDef::Transition t;
        const char* from = yyjson_get_str(yyjson_obj_get(tv, "from"));
        const char* to = yyjson_get_str(yyjson_obj_get(tv, "to"));
        if (!from || !to) return fail(std::format("transition {}: needs from and to", ti + 1));
        t.from = std::string_view(from) == "*" ? -1 : m.FindState(from);
        t.to = m.FindState(to);
        if ((t.from < 0 && std::string_view(from) != "*") || t.to < 0)
          return fail(std::format("transition {}: unknown state '{}'", ti + 1, t.to < 0 ? to : from));
        yyjson_val* when = yyjson_obj_get(tv, "when");
        if (yyjson_is_obj(when)) return fail(std::format("transition {}: graph functions in 'when' come later", ti + 1));
        if (const char* w = yyjson_get_str(when)) t.when = w;
        std::string err;
        if (!ParseAnimCondition(t.when, t.anyOf, &err)) return fail(std::format("transition {}: {}", ti + 1, err));
        for (const auto& g : t.anyOf)
          for (const AnimGraphDef::Cond& c : g)
            if (!out.FindVar(c.var)) return fail(std::format("transition {}: unknown variable '{}'", ti + 1, c.var));
        if (yyjson_val* b = yyjson_obj_get(tv, "blend"); yyjson_is_num(b)) t.blend = std::max(0.f, (float)yyjson_get_num(b));
        if (yyjson_val* x = yyjson_obj_get(tv, "exitTime"); yyjson_is_num(x)) t.exitTime = (float)yyjson_get_num(x);
        m.transitions.push_back(std::move(t));
      }
    }
    out.machines.push_back(std::move(m));
  }
  size_t li, ln;
  yyjson_val* lv;
  yyjson_arr_foreach(yyjson_obj_get(root, "layers"), li, ln, lv) {
    AnimGraphDef::Layer l;
    if (const char* nm = yyjson_get_str(yyjson_obj_get(lv, "name"))) l.name = nm;
    const char* mach = yyjson_get_str(yyjson_obj_get(lv, "machine"));
    l.machine = mach ? out.FindMachine(mach) : -1;
    if (l.machine < 0) return fail(std::format("layer {}: unknown state machine '{}'", li + 1, mach ? mach : ""));
    if (l.name.empty()) l.name = mach;
    if (const char* fb = yyjson_get_str(yyjson_obj_get(lv, "fromBone"))) l.fromBone = fb;
    size_t bi, bn;
    yyjson_val* bv;
    yyjson_arr_foreach(yyjson_obj_get(lv, "bones"), bi, bn, bv)
      if (const char* b = yyjson_get_str(bv)) l.bones.push_back(b);
    if (yyjson_val* w = yyjson_obj_get(lv, "weight"); yyjson_is_num(w)) l.weight = std::clamp((float)yyjson_get_num(w), 0.f, 1.f);
    if (const char* wv = yyjson_get_str(yyjson_obj_get(lv, "weight"))) {
      l.weightVar = wv;
      if (!out.FindVar(wv)) return fail(std::format("layer '{}': unknown variable '{}'", l.name, wv));
    }
    if (const char* mode = yyjson_get_str(yyjson_obj_get(lv, "mode"))) {
      if (std::string_view(mode) == "additive") l.additive = true;
      else if (std::string_view(mode) != "override") return fail(std::format("layer '{}': mode is \"override\" or \"additive\"", l.name));
    }
    out.layers.push_back(std::move(l));
  }
  return true;
}

std::string WriteAnimGraph(const AnimGraphDef& d) {
  std::string o = "{\"v\":1";
  if (!d.skeleton.empty()) o += ", \"skeleton\":" + Quote(d.skeleton);
  o += ",\n \"vars\":{";
  for (size_t i = 0; i < d.vars.size(); ++i) o += (i ? ", " : "") + Quote(d.vars[i].name) + ":" + Quote(TypeName(d.vars[i].type));
  o += "},\n \"stateMachines\":{";
  for (size_t mi = 0; mi < d.machines.size(); ++mi) {
    const AnimGraphDef::Machine& m = d.machines[mi];
    o += (mi ? ",\n  " : "\n  ") + Quote(m.name) + ":{\n";
    o += "   \"entry\":" + Quote(m.states.empty() ? std::string() : m.states[std::clamp(m.entry, 0, (int)m.states.size() - 1)].name) + ",\n";
    o += "   \"states\":{";
    for (size_t i = 0; i < m.states.size(); ++i) {
      const AnimGraphDef::State& s = m.states[i];
      o += (i ? ",\n     " : "\n     ") + Quote(s.name) + ":{";
      if (s.IsBlend()) {
        o += "\"blendSpace\":{\"x\":" + Quote(s.blendX);
        if (!s.blendY.empty()) o += ", \"y\":" + Quote(s.blendY);
        o += ", \"samples\":[";
        for (size_t k = 0; k < s.samples.size(); ++k) {
          o += (k ? ", " : "") + std::string("{\"clip\":") + Quote(s.samples[k].clip) + ", \"x\":" + Num(s.samples[k].x);
          if (!s.blendY.empty()) o += ", \"y\":" + Num(s.samples[k].y);
          o += "}";
        }
        o += "]}";
      } else {
        o += "\"play\":" + Quote(s.clip.empty() ? s.name : s.clip);
      }
      if (!s.loop) o += ", \"loop\":false";
      if (s.speed != 1.f) o += ", \"speed\":" + Num(s.speed);
      if (!s.events.empty()) {
        o += ", \"events\":[";
        for (size_t e = 0; e < s.events.size(); ++e) o += (e ? ", " : "") + std::string("{\"event\":") + Quote(s.events[e].name) + ", \"at\":" + Num(s.events[e].at) + "}";
        o += "]";
      }
      o += "}";
    }
    o += "},\n   \"transitions\":[";
    for (size_t i = 0; i < m.transitions.size(); ++i) {
      const AnimGraphDef::Transition& t = m.transitions[i];
      o += (i ? ",\n     " : "\n     ") + std::string("{\"from\":") + Quote(t.from < 0 ? std::string("*") : m.states[t.from].name) + ", \"to\":" + Quote(m.states[t.to].name);
      if (!t.when.empty()) o += ", \"when\":" + Quote(t.when);
      if (t.exitTime >= 0) o += ", \"exitTime\":" + Num(t.exitTime);
      o += ", \"blend\":" + Num(t.blend) + "}";
    }
    o += "]}";
  }
  o += "}";
  if (!d.layers.empty()) {
    o += ",\n \"layers\":[";
    for (size_t i = 0; i < d.layers.size(); ++i) {
      const AnimGraphDef::Layer& l = d.layers[i];
      o += (i ? ",\n   " : "\n   ") + std::string("{\"name\":") + Quote(l.name) + ", \"machine\":" + Quote(d.machines[l.machine].name);
      if (!l.fromBone.empty()) o += ", \"fromBone\":" + Quote(l.fromBone);
      if (!l.bones.empty()) {
        o += ", \"bones\":[";
        for (size_t b = 0; b < l.bones.size(); ++b) o += (b ? ", " : "") + Quote(l.bones[b]);
        o += "]";
      }
      o += ", \"weight\":" + (l.weightVar.empty() ? Num(l.weight) : Quote(l.weightVar)) + ", \"mode\":" + Quote(l.additive ? "additive" : "override") + "}";
    }
    o += "]";
  }
  o += "}\n";
  return o;
}

// ---- runtime ----

void AnimatorRuntime::Init(const AnimGraphDef* def, DurationFn duration, int machine, std::shared_ptr<Params> params) {
  def_ = def && !def->machines.empty() ? def : nullptr;
  machine_ = def_ ? std::clamp(machine, 0, (int)def_->machines.size() - 1) : 0;
  duration_ = std::move(duration);
  if (params) params_ = std::move(params);
  else {
    params_ = std::make_shared<Params>();
    if (def_)
      for (const AnimGraphDef::Var& v : def_->vars) (*params_)[v.name] = 0.f;
  }
  cur_ = next_ = {};
  fading_ = false;
  fade_ = fadeTime_ = 0;
}

const AnimGraphDef::Machine* AnimatorRuntime::Machine() const { return def_ ? &def_->machines[machine_] : nullptr; }

float AnimatorRuntime::Get(const std::string& name) const {
  auto it = params_->find(name);
  return it == params_->end() ? 0.f : it->second;
}

bool AnimatorRuntime::HasState(const std::string& name) const { return def_ && Machine()->FindState(name) >= 0; }

bool AnimatorRuntime::MakePlay(const std::string& name, Play& p) const {
  p = {};
  if (const AnimGraphDef::Machine* m = Machine()) {
    const int s = m->FindState(name);
    if (s >= 0) {
      const AnimGraphDef::State& st = m->states[s];
      p.state = s;
      p.loop = st.loop;
      p.speed = st.speed;
      p.events = &st.events;
      if (st.IsBlend()) {
        p.blend = true;
        p.duration = 1.f;
        for (const AnimGraphDef::BlendSample& b : st.samples)
          if (!duration_ || duration_(b.clip) < 0) {
            LogWarn("animator: blend state '{}' uses clip '{}', which the model does not have", st.name, b.clip);
            p.duration = -1.f;
          }
        return p.duration >= 0;
      }
      p.clip = st.clip.empty() ? st.name : st.clip;
      p.duration = duration_ ? duration_(p.clip) : -1.f;
      if (p.duration < 0) LogWarn("animator: state '{}' plays clip '{}', which the model does not have", st.name, p.clip);
      return p.duration >= 0;
    }
  }
  p.clip = name;
  p.duration = duration_ && !name.empty() ? duration_(name) : -1.f;
  return p.duration >= 0;
}

void AnimatorRuntime::Begin(Play&& p, float fade) {
  if (fade <= 0.f || !cur_.Valid()) {
    cur_ = std::move(p);
    next_ = {};
    fading_ = false;
    return;
  }
  if (fading_) cur_ = std::move(next_); // interrupted: the state being faded into becomes the one fading out
  next_ = std::move(p);
  fading_ = true;
  fade_ = 0;
  fadeTime_ = fade;
}

void AnimatorRuntime::Start(const std::string& name) {
  Play p;
  std::string n = name;
  if (const AnimGraphDef::Machine* m = Machine(); n.empty() && m && !m->states.empty())
    n = m->states[std::clamp(m->entry, 0, (int)m->states.size() - 1)].name;
  if (!n.empty() && MakePlay(n, p)) Begin(std::move(p), 0.f);
}

bool AnimatorRuntime::PlayState(const std::string& name, float fade) {
  if (!name.empty() && StateName() == name) return true; // already playing it: keep going
  Play p;
  if (!MakePlay(name, p)) return false;
  Begin(std::move(p), fade);
  return true;
}

std::string AnimatorRuntime::StateName() const {
  const Play& p = fading_ ? next_ : cur_;
  if (const AnimGraphDef::Machine* m = Machine(); m && p.state >= 0 && p.state < (int)m->states.size()) return m->states[p.state].name;
  return p.clip;
}

bool AnimatorRuntime::CondTrue(const AnimGraphDef::Transition& t) const {
  for (const auto& group : t.anyOf) {
    bool all = true;
    for (const AnimGraphDef::Cond& c : group) {
      const float v = Get(c.var);
      bool r = false;
      switch (c.op) {
        case AnimGraphDef::Cond::Truthy: r = v != 0.f; break;
        case AnimGraphDef::Cond::Gt: r = v > c.value; break;
        case AnimGraphDef::Cond::Ge: r = v >= c.value; break;
        case AnimGraphDef::Cond::Lt: r = v < c.value; break;
        case AnimGraphDef::Cond::Le: r = v <= c.value; break;
        case AnimGraphDef::Cond::Eq: r = v == c.value; break;
        case AnimGraphDef::Cond::Ne: r = v != c.value; break;
      }
      if (c.negate) r = !r;
      if (!r) { all = false; break; }
    }
    if (all) return true;
  }
  return false;
}

void AnimatorRuntime::Consume(const AnimGraphDef::Transition& t) {
  for (const auto& group : t.anyOf)
    for (const AnimGraphDef::Cond& c : group)
      if (const AnimGraphDef::Var* v = def_->FindVar(c.var); v && v->type == AnimGraphDef::VarType::Trigger) (*params_)[c.var] = 0.f;
}

void AnimatorRuntime::Samples(const Play& p, std::vector<ClipSample>& out) const {
  out.clear();
  if (!p.Valid()) return;
  const AnimGraphDef::Machine* m = Machine();
  if (p.blend && m && p.state >= 0) {
    const AnimGraphDef::State& st = m->states[p.state];
    std::vector<float> w;
    BlendSpaceWeights(st.samples, !st.blendY.empty(), Get(st.blendX), st.blendY.empty() ? 0.f : Get(st.blendY), w);
    for (size_t i = 0; i < st.samples.size(); ++i) {
      if (w[i] <= 1e-5f) continue;
      const float d = std::max(duration_ ? duration_(st.samples[i].clip) : 0.f, 0.f);
      out.push_back({st.samples[i].clip, p.time * d, p.prevTime * d, w[i], d, p.loop, p.wrapped});
    }
    return;
  }
  out.push_back({p.clip, p.time, p.prevTime, 1.f, p.duration, p.loop, p.wrapped});
}

float AnimatorRuntime::BlendDuration(const Play& p) const {
  std::vector<ClipSample> s;
  Samples(p, s);
  float d = 0;
  for (const ClipSample& c : s) d += c.weight * c.duration;
  return d;
}

void AnimatorRuntime::Advance(Play& p, float dt, std::vector<FiredEvent>& events) {
  p.prevTime = p.time;
  p.wrapped = false;
  if (!p.Valid() || p.duration <= 0.f) {
    p.time = 0;
    p.fresh = false;
    return;
  }
  float d = dt * p.speed;
  if (p.blend) d /= std::max(BlendDuration(p), 1e-3f); // normalised time: the mix's own length
  const float t0 = p.time, t1 = p.time + d;
  const std::string stateName = p.state >= 0 && Machine() ? Machine()->states[p.state].name : p.clip;
  if (p.events && d > 0.f)
    for (const AnimGraphDef::Event& e : *p.events) {
      int count = 0;
      if (p.loop) { // crossings of e.at + k * duration in (t0, t1]
        const float lo = std::floor((t0 - e.at) / p.duration - (p.fresh ? 1e-6f : 0.f)), hi = std::floor((t1 - e.at) / p.duration);
        count = (int)std::min(hi - lo, 4.f);
      } else {
        count = ((p.fresh ? t0 <= e.at : t0 < e.at) && e.at <= std::min(t1, p.duration)) ? 1 : 0;
      }
      for (int k = 0; k < count; ++k) events.push_back({e.name, stateName});
    }
  p.wrapped = p.loop && t1 >= p.duration;
  p.time = p.loop ? std::fmod(std::max(t1, 0.f), p.duration) : std::clamp(t1, 0.f, p.duration);
  p.fresh = false;
}

void AnimatorRuntime::Update(float dt, float speed, std::vector<FiredEvent>& events) {
  const float step = dt * speed;
  Advance(cur_, step, events);
  if (fading_) {
    Advance(next_, step, events);
    fade_ += step;
    if (fade_ >= fadeTime_) {
      cur_ = std::move(next_);
      next_ = {};
      fading_ = false;
    }
  }
  const AnimGraphDef::Machine* m = Machine();
  if (fading_ || !m || cur_.state < 0) return;
  const int active = cur_.state;
  for (const AnimGraphDef::Transition& t : m->transitions) {
    if (t.from >= 0 && t.from != active) continue;
    if (t.from < 0 && t.to == active) continue; // "any state" does not re-enter the state it would go to
    if (t.exitTime >= 0.f) {
      const float norm = cur_.duration > 0 ? cur_.time / cur_.duration : 1.f;
      const bool ended = !cur_.loop && cur_.time >= cur_.duration;
      if (norm < t.exitTime && !ended) continue;
    }
    if (!t.anyOf.empty() && !CondTrue(t)) continue;
    Play p;
    if (!MakePlay(m->states[t.to].name, p)) continue;
    Consume(t);
    Begin(std::move(p), t.blend);
    ++transitionsTaken;
    break;
  }
}
}
