#include "Core/Reflection.h"
#include <yyjson.h>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>

namespace bdg {
const char* FieldTypeName(FieldType t) {
  static const char* names[] = {"bool", "int", "float", "double", "vec2", "vec3", "vec4", "quat", "color",
                                "string", "enum", "handle", "struct", "vector"};
  return names[(int)t];
}

const char* EnumInfo::NameOf(int64_t v) const {
  for (auto& [n, value] : values) if (value == v) return n.c_str();
  return nullptr;
}

bool EnumInfo::ValueOf(std::string_view n, int64_t& out) const {
  for (auto& [key, value] : values) if (key == n) { out = value; return true; }
  return false;
}

const FieldInfo* TypeInfo::FindField(std::string_view n) const {
  for (const TypeInfo* t = this; t; t = t->base)
    for (auto& f : t->fields) if (f.name == n) return &f;
  return nullptr;
}

const FunctionInfo* TypeInfo::FindFunction(std::string_view n) const {
  for (const TypeInfo* t = this; t; t = t->base)
    for (auto& f : t->functions) if (f.name == n) return &f;
  return nullptr;
}

bool TypeInfo::IsA(const TypeInfo& other) const {
  for (const TypeInfo* t = this; t; t = t->base) if (t == &other) return true;
  return false;
}

Registry& Registry::Instance() {
  static Registry r;
  return r;
}

void Registry::Add(const TypeInfo& t) {
  for (auto* x : types_) if (x == &t) return;
  types_.push_back(&t);
}

void Registry::AddEnum(const EnumInfo& e) {
  for (auto* x : enums_) if (x == &e) return;
  enums_.push_back(&e);
}

const TypeInfo* Registry::Find(std::string_view name) const {
  for (auto* t : types_) if (t->name == name) return t;
  return nullptr;
}

const EnumInfo* Registry::FindEnum(std::string_view name) const {
  for (auto* e : enums_) if (e->name == name) return e;
  return nullptr;
}

std::vector<Registry::Change> Registry::TakeChanges() {
  std::vector<Change> out;
  out.swap(changes_);
  return out;
}

// ---- Value <-> JSON text -----------------------------------------------------------------------
namespace {
int64_t ReadInt(const void* p, size_t size) {
  switch (size) {
    case 1: return *static_cast<const int8_t*>(p);
    case 2: return *static_cast<const int16_t*>(p);
    case 4: return *static_cast<const int32_t*>(p);
    default: return *static_cast<const int64_t*>(p);
  }
}

void WriteInt(void* p, size_t size, int64_t v) {
  switch (size) {
    case 1: *static_cast<int8_t*>(p) = (int8_t)v; break;
    case 2: *static_cast<int16_t*>(p) = (int16_t)v; break;
    case 4: *static_cast<int32_t*>(p) = (int32_t)v; break;
    default: *static_cast<int64_t*>(p) = v; break;
  }
}

std::string Quote(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) out += std::format("\\u{:04x}", c);
        else out += (char)c;
    }
  }
  return out + "\"";
}

std::string Num(double v) { return std::isfinite(v) ? std::format("{}", v) : "0"; }
std::string NumF(float v) { return std::isfinite(v) ? std::format("{}", v) : "0"; } // shortest float text: 0.45, not 0.449999988
std::string Floats(const float* f, int n) {
  std::string s = "[";
  for (int i = 0; i < n; ++i) s += (i ? "," : "") + NumF(f[i]);
  return s + "]";
}

int Components(FieldType t) {
  switch (t) {
    case FieldType::Vec2: return 2;
    case FieldType::Vec3: return 3;
    case FieldType::Vec4: case FieldType::Quat: case FieldType::Color: return 4;
    default: return 0;
  }
}

bool Fail(std::string* error, std::string msg) {
  if (error) *error = std::move(msg);
  return false;
}

bool FromVal(void* p, const ValueType& vt, yyjson_val* v, std::string* error) {
  switch (vt.type) {
    case FieldType::Bool:
      if (!yyjson_is_bool(v)) return Fail(error, "expected true or false");
      *static_cast<bool*>(p) = yyjson_get_bool(v);
      return true;
    case FieldType::Int32: {
      if (!yyjson_is_num(v)) return Fail(error, "expected an integer");
      double d = yyjson_get_num(v);
      if (d != std::floor(d) || d < std::numeric_limits<int32_t>::min() || d > std::numeric_limits<int32_t>::max())
        return Fail(error, std::format("expected an integer in int range, got {}", d));
      *static_cast<int32_t*>(p) = (int32_t)d;
      return true;
    }
    case FieldType::Float:
      if (!yyjson_is_num(v)) return Fail(error, "expected a number");
      *static_cast<float*>(p) = (float)yyjson_get_num(v);
      return true;
    case FieldType::Double:
      if (!yyjson_is_num(v)) return Fail(error, "expected a number");
      *static_cast<double*>(p) = yyjson_get_num(v);
      return true;
    case FieldType::Vec2: case FieldType::Vec3: case FieldType::Vec4: case FieldType::Quat: case FieldType::Color: {
      int n = Components(vt.type);
      float* f = static_cast<float*>(p);
      if (vt.type == FieldType::Color && yyjson_is_str(v)) { // "#rrggbb" or "#rrggbbaa"
        const char* s = yyjson_get_str(v);
        size_t len = std::strlen(s);
        if (s[0] != '#' || (len != 7 && len != 9)) return Fail(error, "expected a color: [r,g,b,a] or \"#rrggbb\"");
        unsigned long x = std::strtoul(s + 1, nullptr, 16);
        if (len == 7) x = (x << 8) | 0xff;
        for (int i = 0; i < 4; ++i) f[i] = ((x >> (24 - 8 * i)) & 0xff) / 255.f;
        return true;
      }
      if (yyjson_is_obj(v)) { // {"x":1,"y":2} / {"r":..}: only the given components change
        const char* keys = vt.type == FieldType::Color ? "rgba" : "xyzw";
        for (int i = 0; i < n; ++i) {
          char k[2] = {keys[i], 0};
          if (yyjson_val* c = yyjson_obj_get(v, k)) {
            if (!yyjson_is_num(c)) return Fail(error, std::string("component ") + k + " must be a number");
            f[i] = (float)yyjson_get_num(c);
          }
        }
        return true;
      }
      if (!yyjson_is_arr(v) || (int)yyjson_arr_size(v) != n)
        return Fail(error, std::format("expected {} numbers like [{}]", n, n == 2 ? "1,2" : n == 3 ? "1,2,3" : "1,2,3,4"));
      for (int i = 0; i < n; ++i) {
        yyjson_val* c = yyjson_arr_get(v, (size_t)i);
        if (!yyjson_is_num(c)) return Fail(error, "vector components must be numbers");
        f[i] = (float)yyjson_get_num(c);
      }
      return true;
    }
    case FieldType::String:
      if (!yyjson_is_str(v)) return Fail(error, "expected a string");
      *static_cast<std::string*>(p) = yyjson_get_str(v);
      return true;
    case FieldType::Enum: {
      int64_t value = 0;
      if (yyjson_is_str(v)) {
        if (!vt.enumType->ValueOf(yyjson_get_str(v), value)) {
          std::string names;
          for (auto& [n, x] : vt.enumType->values) names += (names.empty() ? "" : "|") + n;
          return Fail(error, std::format("unknown {} value \"{}\" (use {})", vt.enumType->name, yyjson_get_str(v), names));
        }
      } else if (yyjson_is_int(v)) {
        value = yyjson_get_sint(v);
        if (!vt.enumType->NameOf(value)) return Fail(error, std::format("{} has no value {}", vt.enumType->name, value));
      } else {
        return Fail(error, "expected an enum name");
      }
      WriteInt(p, vt.size, value);
      return true;
    }
    case FieldType::Handle:
      if (!yyjson_is_uint(v) && !yyjson_is_int(v)) return Fail(error, "expected an object id (integer)");
      static_cast<ObjectHandle*>(p)->value = yyjson_get_uint(v);
      return true;
    case FieldType::Struct: {
      if (!yyjson_is_obj(v)) return Fail(error, "expected an object {\"field\": value}");
      size_t i, n;
      yyjson_val *k, *val;
      yyjson_obj_foreach(v, i, n, k, val) {
        const FieldInfo* f = vt.structType->FindField(yyjson_get_str(k));
        if (!f) return Fail(error, std::format("{} has no field \"{}\"", vt.structType->name, yyjson_get_str(k)));
        if (!FromVal(static_cast<char*>(p) + f->offset, f->value, val, error)) {
          if (error) *error = std::string(yyjson_get_str(k)) + ": " + *error;
          return false;
        }
      }
      return true;
    }
    case FieldType::Vector: {
      if (!yyjson_is_arr(v)) return Fail(error, "expected an array");
      size_t n = yyjson_arr_size(v);
      vt.vecResize(p, n);
      for (size_t i = 0; i < n; ++i)
        if (!FromVal(vt.vecAt(p, i), *vt.elem, yyjson_arr_get(v, i), error)) {
          if (error) *error = std::format("[{}]: {}", i, *error);
          return false;
        }
      return true;
    }
  }
  return Fail(error, "unsupported type");
}
}

std::string Registry::ToJson(const void* p, const ValueType& vt) {
  switch (vt.type) {
    case FieldType::Bool: return *static_cast<const bool*>(p) ? "true" : "false";
    case FieldType::Int32: return std::to_string(*static_cast<const int32_t*>(p));
    case FieldType::Float: return NumF(*static_cast<const float*>(p));
    case FieldType::Double: return Num(*static_cast<const double*>(p));
    case FieldType::Vec2: case FieldType::Vec3: case FieldType::Vec4: case FieldType::Quat: case FieldType::Color:
      return Floats(static_cast<const float*>(p), Components(vt.type));
    case FieldType::String: return Quote(*static_cast<const std::string*>(p));
    case FieldType::Enum: {
      int64_t v = ReadInt(p, vt.size);
      const char* n = vt.enumType->NameOf(v);
      return n ? Quote(n) : std::to_string(v);
    }
    case FieldType::Handle: return std::to_string(static_cast<const ObjectHandle*>(p)->value);
    case FieldType::Struct: {
      std::vector<const TypeInfo*> chain; // base fields first
      for (const TypeInfo* t = vt.structType; t; t = t->base) chain.insert(chain.begin(), t);
      std::string s = "{";
      for (const TypeInfo* t : chain)
        for (auto& f : t->fields) s += (s.size() > 1 ? "," : "") + Quote(f.name) + ":" + ToJson(static_cast<const char*>(p) + f.offset, f.value);
      return s + "}";
    }
    case FieldType::Vector: {
      std::string s = "[";
      size_t n = vt.vecSize(p);
      for (size_t i = 0; i < n; ++i) s += (i ? "," : "") + ToJson(vt.vecAt(const_cast<void*>(p), i), *vt.elem);
      return s + "]";
    }
  }
  return "null";
}

bool Registry::FromJson(void* p, const ValueType& vt, std::string_view json, std::string* error) {
  yyjson_doc* doc = yyjson_read(json.data(), json.size(), YYJSON_READ_ALLOW_INF_AND_NAN);
  if (!doc) {
    // Convenience for the terminal: bare words are strings / enum names (set g1 Light.kind=Spot).
    if (vt.type == FieldType::String || vt.type == FieldType::Enum) {
      std::string quoted = Quote(std::string(json));
      doc = yyjson_read(quoted.data(), quoted.size(), 0);
    }
    if (!doc) return Fail(error, std::format("not a valid value: {}", json));
  }
  bool ok = FromVal(p, vt, yyjson_doc_get_root(doc), error);
  yyjson_doc_free(doc);
  return ok;
}

// ---- Paths: "field", "a.b", "items[2]", "pos.x", "tint.r" ---------------------------------------
namespace {
struct Target {
  void* ptr = nullptr;
  const ValueType* vt = nullptr;
  ValueType component; // storage for a vector/colour component (float)
};

bool Resolve(void* obj, const TypeInfo& type, std::string_view path, Target& out, std::string* error) {
  static thread_local ValueType rootType;
  rootType = {};
  rootType.type = FieldType::Struct;
  rootType.structType = &type;
  void* p = obj;
  const ValueType* vt = &rootType;
  size_t i = 0;
  if (path.empty()) return Fail(error, "empty path");
  while (i < path.size()) {
    size_t end = path.find_first_of(".[", i);
    std::string_view name = path.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i);
    if (!name.empty()) {
      if (vt->type == FieldType::Struct) {
        const FieldInfo* f = vt->structType->FindField(name);
        if (!f) {
          std::string names;
          for (const TypeInfo* t = vt->structType; t; t = t->base)
            for (auto& x : t->fields) names += (names.empty() ? "" : ", ") + x.name;
          return Fail(error, std::format("{} has no field \"{}\" (fields: {})", vt->structType->name, name, names));
        }
        p = static_cast<char*>(p) + f->offset;
        vt = &f->value;
      } else if (int n = Components(vt->type); n > 0 && name.size() == 1) {
        const char* keys = vt->type == FieldType::Color ? "rgba" : "xyzw";
        const char* k = std::strchr(keys, name[0]);
        if (!k || k - keys >= n) return Fail(error, std::format("{} has no component \"{}\"", FieldTypeName(vt->type), name));
        p = static_cast<float*>(p) + (k - keys);
        out.component = {};
        out.component.type = FieldType::Float;
        out.component.size = sizeof(float);
        vt = &out.component;
      } else {
        return Fail(error, std::format("cannot look up \"{}\" inside a {}", name, FieldTypeName(vt->type)));
      }
    }
    if (end == std::string_view::npos) break;
    if (path[end] == '.') { i = end + 1; continue; }
    size_t close = path.find(']', end);                      // [index]
    if (close == std::string_view::npos) return Fail(error, "missing ] in path");
    if (vt->type != FieldType::Vector) return Fail(error, std::format("[index] needs an array, not a {}", FieldTypeName(vt->type)));
    std::string idx(path.substr(end + 1, close - end - 1));
    char* stop = nullptr;
    long index = std::strtol(idx.c_str(), &stop, 10);
    size_t size = vt->vecSize(p);
    if (idx.empty() || *stop || index < 0 || (size_t)index >= size)
      return Fail(error, std::format("index [{}] out of range (size {})", idx, size));
    p = vt->vecAt(p, (size_t)index);
    vt = vt->elem;
    i = close + 1;
    if (i < path.size() && path[i] == '.') ++i;
  }
  out.ptr = p;
  out.vt = vt;
  return true;
}
}

bool Registry::Get(const void* obj, const TypeInfo& type, std::string_view path, std::string& json, std::string* error) {
  Target t;
  if (!Resolve(const_cast<void*>(obj), type, path, t, error)) return false;
  json = ToJson(t.ptr, *t.vt);
  return true;
}

bool Registry::Set(void* obj, const TypeInfo& type, std::string_view path, std::string_view json, std::string* error) {
  Target t;
  if (!Resolve(obj, type, path, t, error)) return false;
  std::string before = ToJson(t.ptr, *t.vt);
  if (!FromJson(t.ptr, *t.vt, json, error)) {
    FromJson(t.ptr, *t.vt, before, nullptr); // a half-applied struct/array write is rolled back
    if (error) *error = std::format("{}.{}: {}", type.name, path, *error);
    return false;
  }
  if (ToJson(t.ptr, *t.vt) != before) Instance().changes_.push_back({obj, &type, std::string(path)});
  return true;
}

bool Registry::Call(void* obj, const TypeInfo& type, std::string_view function, const std::vector<std::string>& args,
                    std::string* resultJson, std::string* error) {
  const FunctionInfo* f = type.FindFunction(function);
  if (!f) return Fail(error, std::format("{} has no function \"{}\"", type.name, function));
  return f->invoke(obj, args, resultJson, error);
}
}
