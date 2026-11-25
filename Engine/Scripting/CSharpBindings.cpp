#include "Scripting/CSharpBindings.h"
#include "Core/Reflection.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <set>

namespace bdg {
namespace {
bool IsComponent(const TypeInfo& t) { return std::find(t.tags.begin(), t.tags.end(), "Component") != t.tags.end(); }

std::string Pascal(const std::string& s) {
  std::string o;
  bool up = true;
  for (char c : s) {
    if (c == '_') { up = true; continue; }
    o += up ? (char)std::toupper((unsigned char)c) : c;
    up = false;
  }
  return o.empty() ? "Value" : o;
}

bool IsKeyword(const std::string& s) {
  static const std::set<std::string> k = {"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked",
      "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit",
      "extern", "false", "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface",
      "internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override", "params",
      "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc",
      "static", "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked",
      "unsafe", "ushort", "using", "virtual", "void", "volatile", "while"};
  return k.count(s) != 0;
}
std::string Ident(const std::string& s) { return IsKeyword(s) ? "@" + s : s; }

// C# type of a reflected value; JsonNode for structs and arrays (typed wrappers come with 4-3).
std::string CsType(const ValueType& v, std::set<const EnumInfo*>& enums) {
  switch (v.type) {
    case FieldType::Bool: return "bool";
    case FieldType::Int32: return "int";
    case FieldType::Float: return "float";
    case FieldType::Double: return "double";
    case FieldType::Vec2: return "Vector2";
    case FieldType::Vec3: return "Vector3";
    case FieldType::Vec4: case FieldType::Color: return "Vector4";
    case FieldType::Quat: return "Quaternion";
    case FieldType::String: return "string";
    case FieldType::Handle: return "ulong";
    case FieldType::Enum:
      if (v.enumType) {
        enums.insert(v.enumType);
        return v.enumType->name;
      }
      return "int";
    case FieldType::Struct: case FieldType::Vector: return "JsonNode?";
  }
  return "JsonNode?";
}

std::string Escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c != '\n' && c != '\r') o += c;
  }
  return o;
}
} // namespace

std::string GenerateCSharpBindings() {
  std::map<std::string, const TypeInfo*> comps;
  for (const TypeInfo* t : Registry::Instance().Types())
    if (IsComponent(*t)) comps[t->name] = t;
  std::set<const EnumInfo*> enums;
  std::string body;
  for (const auto& [name, t] : comps) {
    std::vector<const TypeInfo*> chain;
    for (const TypeInfo* x = t; x; x = x->base) chain.insert(chain.begin(), x);
    std::set<std::string> used = {"GameObject", "TypeName", name}; // members of Component and the class name
    std::string members;
    for (const TypeInfo* x : chain)
      for (const FieldInfo& f : x->fields) {
        if (!(f.flags & (kEdit | kReadOnly))) continue;
        std::string prop = Pascal(f.name);
        while (used.count(prop)) prop += "_";
        used.insert(prop);
        const std::string type = CsType(f.value, enums);
        if (!f.meta.tooltip.empty()) members += std::format("  /// <summary>{}</summary>\n", Escape(f.meta.tooltip));
        if (f.flags & kReadOnly) members += std::format("  public {} {} => Get<{}>(\"{}\");\n", type, prop, type, f.name);
        else
          members += std::format("  public {} {} {{ get => Get<{}>(\"{}\"); set => Set(\"{}\", value); }}\n", type, prop, type,
                                 f.name, f.name);
      }
    for (const TypeInfo* x : chain)
      for (const FunctionInfo& fn : x->functions) {
        if (!(fn.flags & (kCallable | kPure | kButton))) continue;
        std::string method = Pascal(fn.name);
        while (used.count(method)) method += "_";
        used.insert(method);
        std::string params, args;
        for (size_t i = 0; i < fn.params.size(); ++i) {
          const std::string pn = i < fn.paramNames.size() && !fn.paramNames[i].empty() ? fn.paramNames[i] : std::format("arg{}", i);
          params += std::format("{}{} {}", i ? ", " : "", CsType(*fn.params[i], enums), Ident(pn));
          args += ", " + Ident(pn);
        }
        if (fn.ret) {
          const std::string rt = CsType(*fn.ret, enums);
          members += std::format("  public {} {}({}) => Call<{}>(\"{}\"{});\n", rt, method, params, rt, fn.name, args);
        } else
          members += std::format("  public void {}({}) => CallVoid(\"{}\"{});\n", method, params, fn.name, args);
      }
    body += std::format("\npublic sealed partial class {} : Component {{\n  public {}(GameObject owner) : base(owner, \"{}\") {{}}\n{}}}\n",
                        name, name, name, members);
  }
  std::map<std::string, const EnumInfo*> sortedEnums;
  for (const EnumInfo* e : enums) sortedEnums[e->name] = e;
  std::string enumText;
  for (const auto& [name, e] : sortedEnums) {
    enumText += std::format("\npublic enum {} {{", name);
    for (size_t i = 0; i < e->values.size(); ++i)
      enumText += std::format("{} {} = {}", i ? "," : "", e->values[i].first, e->values[i].second);
    enumText += " }\n";
  }
  return "// <auto-generated> by `bdg csharp-bindings` from the engine's reflection (queue 4-1). Do not edit.\n"
         "#nullable enable\n"
         "using System.Numerics;\n"
         "using System.Text.Json.Nodes;\n\n"
         "namespace Bidulgi;\n" +
         enumText + body;
}

} // namespace bdg
