#include "Scene/GraphLibrary.h"
#include "Core/Reflection.h"
#include <algorithm>
#include <cctype>
#include <format>

namespace bdg {
namespace {
std::string TypeName(const ValueType& v) {
  if (v.type == FieldType::Enum && v.enumType) return v.enumType->name;
  if (v.type == FieldType::Struct && v.structType) return v.structType->name;
  if (v.type == FieldType::Vector && v.elem) return "array<" + TypeName(*v.elem) + ">";
  return FieldTypeName(v.type);
}
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string Q(const std::string& s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o + "\"";
}
bool IsComponent(const TypeInfo& t) { return std::find(t.tags.begin(), t.tags.end(), "Component") != t.tags.end(); }
} // namespace

std::string GraphNodeDesc::Signature() const {
  auto pins = [](const std::vector<GraphPin>& ps) {
    std::string s;
    for (const GraphPin& p : ps) s += (s.empty() ? "" : ", ") + (p.type == "exec" ? std::string("exec") : p.name + ": " + p.type);
    return s;
  };
  std::string s = id + "(" + pins(inputs) + ")";
  if (!outputs.empty()) s += " -> " + pins(outputs);
  return s;
}

std::vector<GraphNodeDesc> BuildGraphNodeLibrary(const std::string& filter) {
  std::vector<GraphNodeDesc> out;
  const std::string f = Lower(filter);
  for (const TypeInfo* t : Registry::Instance().Types()) {
    if (!IsComponent(*t)) continue;
    std::vector<const TypeInfo*> chain; // base members first, the way the Inspector lists them
    for (const TypeInfo* x = t; x; x = x->base) chain.insert(chain.begin(), x);
    for (const TypeInfo* x : chain) {
      for (const FunctionInfo& fn : x->functions) {
        if (!(fn.flags & kCallable)) continue;
        GraphNodeDesc n;
        n.kind = (fn.flags & kPure) ? GraphNodeDesc::Kind::Pure : GraphNodeDesc::Kind::Call;
        n.component = t->name;
        n.member = fn.name;
        n.id = t->name + "." + fn.name;
        if (n.kind == GraphNodeDesc::Kind::Call) n.inputs.push_back({"exec", "exec"});
        n.inputs.push_back({"target", "object"});
        for (size_t i = 0; i < fn.params.size(); ++i)
          n.inputs.push_back({i < fn.paramNames.size() ? fn.paramNames[i] : std::format("arg{}", i), TypeName(*fn.params[i])});
        if (n.kind == GraphNodeDesc::Kind::Call) n.outputs.push_back({"exec", "exec"});
        if (fn.ret) n.outputs.push_back({"result", TypeName(*fn.ret)});
        out.push_back(std::move(n));
      }
      for (const FieldInfo& fd : x->fields) {
        if (!(fd.flags & kEdit)) continue;
        GraphNodeDesc g;
        g.kind = GraphNodeDesc::Kind::Get;
        g.component = t->name;
        g.member = fd.name;
        g.id = t->name + ".Get " + fd.name;
        g.inputs.push_back({"target", "object"});
        g.outputs.push_back({fd.name, TypeName(fd.value)});
        out.push_back(g);
        if (fd.flags & kReadOnly) continue;
        GraphNodeDesc s;
        s.kind = GraphNodeDesc::Kind::Set;
        s.component = t->name;
        s.member = fd.name;
        s.id = t->name + ".Set " + fd.name;
        s.inputs = {{"exec", "exec"}, {"target", "object"}, {fd.name, TypeName(fd.value)}};
        s.outputs = {{"exec", "exec"}};
        out.push_back(std::move(s));
      }
    }
  }
  if (!f.empty()) std::erase_if(out, [&](const GraphNodeDesc& n) { return Lower(n.id).find(f) == std::string::npos; });
  std::stable_sort(out.begin(), out.end(), [](const GraphNodeDesc& a, const GraphNodeDesc& b) { return a.id < b.id; });
  return out;
}

const GraphNodeDesc* FindGraphNode(const std::vector<GraphNodeDesc>& lib, const std::string& id) {
  for (const GraphNodeDesc& n : lib)
    if (n.id == id || Lower(n.id) == Lower(id)) return &n;
  return nullptr;
}

bool RunGraphNode(CommandBus& bus, Scene& scene, const GraphNodeDesc& node, const std::string& target, const std::vector<std::string>& args,
                  std::string* resultJson, std::string* error) {
  auto fail = [&](std::string why) {
    if (error) *error = std::move(why);
    return false;
  };
  SceneObject* o = scene.Find(target);
  if (!o) return fail(std::format("no object \"{}\"", target));
  Component* c = o->Find(node.component);
  if (!c) return fail(std::format("\"{}\" has no component \"{}\"", o->name, node.component));
  size_t expected = 0;
  for (const GraphPin& p : node.inputs) expected += p.type != "exec" && p.type != "object";
  if (args.size() != expected) return fail(std::format("{} takes {} argument(s), got {}", node.id, expected, args.size()));
  switch (node.kind) {
    case GraphNodeDesc::Kind::Pure: // a value: read only, no command
      return Registry::Call(c->data, *c->type, node.member, args, resultJson, error);
    case GraphNodeDesc::Kind::Get: {
      std::string v;
      if (!Registry::Get(c->data, *c->type, node.member, v, error)) return false;
      if (resultJson) *resultJson = v;
      return true;
    }
    case GraphNodeDesc::Kind::Set: {
      Command cmd;
      cmd.name = "set";
      cmd.target = target;
      cmd.args = std::format(R"({{"path":{},"value":{}}})", Q(node.component + "." + node.member), args[0]);
      cmd.source = "script";
      return bus.Execute(cmd, error);
    }
    case GraphNodeDesc::Kind::Call: {
      std::string list;
      for (const std::string& a : args) list += (list.empty() ? "" : ",") + a;
      Command cmd;
      cmd.name = "call";
      cmd.target = target;
      cmd.args = std::format(R"({{"type":{},"function":{},"args":[{}]}})", Q(node.component), Q(node.member), list);
      cmd.source = "script";
      if (!bus.Execute(cmd, error)) return false;
      if (resultJson) *resultJson = bus.LastResult();
      return true;
    }
  }
  return fail("unknown node kind");
}

} // namespace bdg
