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

int AnimGraphDef::FindState(std::string_view name) const {
  for (size_t i = 0; i < states.size(); ++i)
    if (states[i].name == name) return (int)i;
  return -1;
}

const AnimGraphDef::Var* AnimGraphDef::FindVar(std::string_view name) const {
  for (const Var& v : vars)
    if (v.name == name) return &v;
  return nullptr;
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
  if (yyjson_obj_size(machines) > 1) LogWarn("animgraph: only the first state machine plays for now (7-1)");
  yyjson_obj_iter it = yyjson_obj_iter_with(machines);
  yyjson_val* key = yyjson_obj_iter_next(&it);
  yyjson_val* sm = yyjson_obj_iter_get_val(key);
  out.machine = yyjson_get_str(key);
  if (!yyjson_is_obj(sm)) return fail("state machine is not an object");
  yyjson_val* states = yyjson_obj_get(sm, "states");
  if (!yyjson_is_obj(states) || yyjson_obj_size(states) == 0) return fail(std::format("{}: no states", out.machine));
  {
    size_t i, n;
    yyjson_val *k, *v;
    yyjson_obj_foreach(states, i, n, k, v) {
      AnimGraphDef::State st;
      st.name = yyjson_get_str(k);
      if (const char* p = yyjson_get_str(yyjson_obj_get(v, "play"))) st.clip = p;
      if (yyjson_val* b = yyjson_obj_get(v, "loop"); yyjson_is_bool(b)) st.loop = yyjson_get_bool(b);
      if (yyjson_val* s = yyjson_obj_get(v, "speed"); yyjson_is_num(s)) st.speed = (float)yyjson_get_num(s);
      if (yyjson_obj_get(v, "blendSpace")) LogWarn("animgraph: state '{}': blend spaces come later (7-1b); it plays its 'play' clip", st.name);
      if (yyjson_val* evs = yyjson_obj_get(v, "events"); yyjson_is_arr(evs)) {
        size_t ei, en;
        yyjson_val* e;
        yyjson_arr_foreach(evs, ei, en, e) {
          AnimGraphDef::Event ev;
          if (const char* nm = yyjson_get_str(yyjson_obj_get(e, "event"))) ev.name = nm;
          ev.at = (float)yyjson_get_num(yyjson_obj_get(e, "at"));
          if (ev.name.empty()) return fail(std::format("state '{}': an event without a name", st.name));
          st.events.push_back(std::move(ev));
        }
      }
      out.states.push_back(std::move(st));
    }
  }
  if (const char* e = yyjson_get_str(yyjson_obj_get(sm, "entry"))) {
    out.entry = out.FindState(e);
    if (out.entry < 0) return fail(std::format("entry state '{}' does not exist", e));
  }
  if (yyjson_val* tr = yyjson_obj_get(sm, "transitions"); yyjson_is_arr(tr)) {
    size_t i, n;
    yyjson_val* v;
    yyjson_arr_foreach(tr, i, n, v) {
      AnimGraphDef::Transition t;
      const char* from = yyjson_get_str(yyjson_obj_get(v, "from"));
      const char* to = yyjson_get_str(yyjson_obj_get(v, "to"));
      if (!from || !to) return fail(std::format("transition {}: needs from and to", i + 1));
      t.from = std::string_view(from) == "*" ? -1 : out.FindState(from);
      t.to = out.FindState(to);
      if ((t.from < 0 && std::string_view(from) != "*") || t.to < 0)
        return fail(std::format("transition {}: unknown state '{}'", i + 1, t.to < 0 ? to : from));
      yyjson_val* when = yyjson_obj_get(v, "when");
      if (yyjson_is_obj(when)) return fail(std::format("transition {}: graph functions in 'when' come later", i + 1));
      if (const char* w = yyjson_get_str(when)) t.when = w;
      std::string err;
      if (!ParseAnimCondition(t.when, t.anyOf, &err)) return fail(std::format("transition {}: {}", i + 1, err));
      for (const auto& g : t.anyOf)
        for (const AnimGraphDef::Cond& c : g)
          if (!out.FindVar(c.var)) return fail(std::format("transition {}: unknown variable '{}'", i + 1, c.var));
      if (yyjson_val* b = yyjson_obj_get(v, "blend"); yyjson_is_num(b)) t.blend = std::max(0.f, (float)yyjson_get_num(b));
      if (yyjson_val* x = yyjson_obj_get(v, "exitTime"); yyjson_is_num(x)) t.exitTime = (float)yyjson_get_num(x);
      out.transitions.push_back(std::move(t));
    }
  }
  return true;
}

std::string WriteAnimGraph(const AnimGraphDef& d) {
  std::string o = "{\"v\":1";
  if (!d.skeleton.empty()) o += ", \"skeleton\":" + Quote(d.skeleton);
  o += ",\n \"vars\":{";
  for (size_t i = 0; i < d.vars.size(); ++i) o += (i ? ", " : "") + Quote(d.vars[i].name) + ":" + Quote(TypeName(d.vars[i].type));
  o += "},\n \"stateMachines\":{" + Quote(d.machine) + ":{\n";
  o += "   \"entry\":" + Quote(d.states.empty() ? std::string() : d.states[std::clamp(d.entry, 0, (int)d.states.size() - 1)].name) + ",\n";
  o += "   \"states\":{";
  for (size_t i = 0; i < d.states.size(); ++i) {
    const AnimGraphDef::State& s = d.states[i];
    o += (i ? ",\n     " : "\n     ") + Quote(s.name) + ":{\"play\":" + Quote(s.clip.empty() ? s.name : s.clip);
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
  for (size_t i = 0; i < d.transitions.size(); ++i) {
    const AnimGraphDef::Transition& t = d.transitions[i];
    o += (i ? ",\n     " : "\n     ") + std::string("{\"from\":") + Quote(t.from < 0 ? std::string("*") : d.states[t.from].name) + ", \"to\":" + Quote(d.states[t.to].name);
    if (!t.when.empty()) o += ", \"when\":" + Quote(t.when);
    if (t.exitTime >= 0) o += ", \"exitTime\":" + Num(t.exitTime);
    o += ", \"blend\":" + Num(t.blend) + "}";
  }
  o += "]}}}\n";
  return o;
}

// ---- runtime ----

void AnimatorRuntime::Init(const AnimGraphDef* def, DurationFn duration) {
  def_ = def;
  duration_ = std::move(duration);
  params_.clear();
  if (def_)
    for (const AnimGraphDef::Var& v : def_->vars) params_[v.name] = 0.f;
  cur_ = next_ = {};
  fading_ = false;
  fade_ = fadeTime_ = 0;
}

float AnimatorRuntime::Get(const std::string& name) const {
  auto it = params_.find(name);
  return it == params_.end() ? 0.f : it->second;
}

bool AnimatorRuntime::MakePlay(const std::string& name, Play& p) const {
  p = {};
  if (def_) {
    const int s = def_->FindState(name);
    if (s >= 0) {
      const AnimGraphDef::State& st = def_->states[s];
      p.state = s;
      p.clip = st.clip.empty() ? st.name : st.clip;
      p.loop = st.loop;
      p.speed = st.speed;
      p.events = &st.events;
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
  if (n.empty() && def_ && !def_->states.empty()) n = def_->states[std::clamp(def_->entry, 0, (int)def_->states.size() - 1)].name;
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
  if (p.state >= 0 && def_ && p.state < (int)def_->states.size()) return def_->states[p.state].name;
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
      if (const AnimGraphDef::Var* v = def_->FindVar(c.var); v && v->type == AnimGraphDef::VarType::Trigger) params_[c.var] = 0.f;
}

void AnimatorRuntime::Advance(Play& p, float dt, std::vector<FiredEvent>& events, const std::string& stateName) {
  p.prevTime = p.time;
  if (!p.Valid() || p.duration <= 0.f) {
    p.time = 0;
    p.fresh = false;
    return;
  }
  const float d = dt * p.speed;
  const float t0 = p.time, t1 = p.time + d;
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
  p.time = p.loop ? std::fmod(std::max(t1, 0.f), p.duration) : std::clamp(t1, 0.f, p.duration);
  p.fresh = false;
}

void AnimatorRuntime::Update(float dt, float speed, std::vector<FiredEvent>& events) {
  const float step = dt * speed;
  auto nameOf = [&](const Play& p) { return p.state >= 0 && def_ ? def_->states[p.state].name : p.clip; };
  Advance(cur_, step, events, nameOf(cur_));
  if (fading_) {
    Advance(next_, step, events, nameOf(next_));
    fade_ += step;
    if (fade_ >= fadeTime_) {
      cur_ = std::move(next_);
      next_ = {};
      fading_ = false;
    }
  }
  if (fading_ || !def_ || cur_.state < 0) return;
  const int active = cur_.state;
  for (const AnimGraphDef::Transition& t : def_->transitions) {
    if (t.from >= 0 && t.from != active) continue;
    if (t.from < 0 && t.to == active) continue; // "any state" does not re-enter the state it would go to
    if (t.exitTime >= 0.f) {
      const float norm = cur_.duration > 0 ? cur_.time / cur_.duration : 1.f;
      const bool ended = !cur_.loop && cur_.time >= cur_.duration;
      if (norm < t.exitTime && !ended) continue;
    }
    if (!t.anyOf.empty() && !CondTrue(t)) continue;
    Play p;
    if (!MakePlay(def_->states[t.to].name, p)) continue;
    Consume(t);
    Begin(std::move(p), t.blend);
    ++transitionsTaken;
    break;
  }
}
}
