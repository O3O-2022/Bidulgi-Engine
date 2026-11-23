// Reflection registry tests (queue 1-2 exit criterion).
#include "Tests/BTestComponent.h"
#include "bdg_test.h"

using namespace bdgtest;
using bdg::FieldType;
using bdg::Registry;

TEST_CASE("reflection: type info is generated") {
  const bdg::TypeInfo* t = Registry::Instance().Find("BTestComponent");
  if (!CHECK(t != nullptr)) return;
  CHECK(t == &BTestComponent::StaticType());
  CHECK_EQ(t->fields.size(), size_t(6));
  CHECK_EQ(t->size, sizeof(BTestComponent));
  CHECK(!t->tags.empty() && t->tags[0] == "Component");
  struct Expect { const char* name; FieldType type; };
  Expect expect[] = {{"hp", FieldType::Int32}, {"speed", FieldType::Float}, {"invincible", FieldType::Bool},
                     {"title", FieldType::String}, {"offset", FieldType::Vec3}, {"kind", FieldType::Enum}};
  for (size_t i = 0; i < 6 && i < t->fields.size(); ++i) {
    CHECK_EQ(t->fields[i].name, std::string(expect[i].name));
    CHECK(t->fields[i].value.type == expect[i].type);
    CHECK(t->fields[i].flags & bdg::kEdit);
    CHECK(t->fields[i].flags & bdg::kSave);
  }
  const bdg::FieldInfo* hp = t->FindField("hp");
  CHECK(hp && hp->meta.hasRange && hp->meta.rangeMin == 0 && hp->meta.rangeMax == 1000);
  CHECK(hp && hp->meta.tooltip == "Hit points" && hp->meta.category == "Stats");
  CHECK(t->FindField("offset")->meta.showIf == "invincible");
  CHECK(t->FindField("speed")->flags & bdg::kAnimatable);
  CHECK(t->FindField("invincible")->flags & bdg::kReplicated);
  CHECK(t->FindField("notReflected") == nullptr);
  const bdg::EnumInfo* e = t->FindField("kind")->value.enumType;
  CHECK(e && e->name == "LightKind" && e->values.size() == 3 && e->values[2].second == 5);
  CHECK(Registry::Instance().FindEnum("LightKind") == e);
  if (!CHECK_EQ(t->functions.size(), size_t(1))) return;
  CHECK_EQ(t->functions[0].name, std::string("Heal"));
  CHECK(t->functions[0].paramNames == std::vector<std::string>{"amount"});
  CHECK(t->functions[0].ret && t->functions[0].ret->type == FieldType::Int32);
}

TEST_CASE("reflection: get values by name") {
  BTestComponent c;
  const auto& t = BTestComponent::StaticType();
  std::string v;
  CHECK(Registry::Get(&c, t, "hp", v)); CHECK_EQ(v, std::string("100"));
  CHECK(Registry::Get(&c, t, "speed", v)); CHECK_EQ(v, std::string("1.5"));
  CHECK(Registry::Get(&c, t, "invincible", v)); CHECK_EQ(v, std::string("false"));
  CHECK(Registry::Get(&c, t, "title", v)); CHECK_EQ(v, std::string("\"Knight\""));
  CHECK(Registry::Get(&c, t, "offset", v)); CHECK_EQ(v, std::string("[0,1,0]"));
  CHECK(Registry::Get(&c, t, "offset.y", v)); CHECK_EQ(v, std::string("1"));
  CHECK(Registry::Get(&c, t, "kind", v)); CHECK_EQ(v, std::string("\"Point\""));
  std::string err;
  CHECK(!Registry::Get(&c, t, "mana", v, &err));
  CHECK(err.find("no field \"mana\"") != std::string::npos && err.find("hp") != std::string::npos);
}

TEST_CASE("reflection: set values by name") {
  BTestComponent c;
  const auto& t = BTestComponent::StaticType();
  Registry::Instance().TakeChanges();
  CHECK(Registry::Set(&c, t, "hp", "20")); CHECK_EQ(c.hp, 20);
  CHECK(Registry::Set(&c, t, "speed", "2.25")); CHECK_EQ(c.speed, 2.25f);
  CHECK(Registry::Set(&c, t, "invincible", "true")); CHECK(c.invincible);
  CHECK(Registry::Set(&c, t, "title", "\"Hero 영웅\"")); CHECK_EQ(c.title, std::string("Hero 영웅"));
  CHECK(Registry::Set(&c, t, "offset", "[1,2,3]")); CHECK(c.offset == (bdg::Vec3{1, 2, 3}));
  CHECK(Registry::Set(&c, t, "offset.z", "9")); CHECK_EQ(c.offset.z, 9.f);
  CHECK(Registry::Set(&c, t, "kind", "\"Spot\"")); CHECK(c.kind == LightKind::Spot);
  CHECK(Registry::Set(&c, t, "kind", "Directional")); CHECK(c.kind == LightKind::Directional); // bare word
  std::string err;
  CHECK(!Registry::Set(&c, t, "hp", "\"lots\"", &err)); CHECK_EQ(c.hp, 20);
  CHECK(err.find("BTestComponent.hp") != std::string::npos);
  CHECK(!Registry::Set(&c, t, "hp", "2.5", &err)); CHECK_EQ(c.hp, 20);
  CHECK(!Registry::Set(&c, t, "kind", "Laser", &err));
  CHECK(err.find("Directional|Point|Spot") != std::string::npos);
  CHECK(!Registry::Set(&c, t, "offset", "[1,2]", &err)); CHECK(c.offset == (bdg::Vec3{1, 2, 9}));
}

TEST_CASE("reflection: writes are tracked") {
  BTestComponent c;
  const auto& t = BTestComponent::StaticType();
  Registry::Instance().TakeChanges();
  Registry::Set(&c, t, "hp", "50");
  Registry::Set(&c, t, "hp", "50");        // same value: not a change
  Registry::Set(&c, t, "offset.x", "4");
  Registry::Set(&c, t, "hp", "\"bad\"");   // failed write: not a change
  auto changes = Registry::Instance().TakeChanges();
  if (!CHECK_EQ(changes.size(), size_t(2))) return;
  CHECK(changes[0].object == &c && changes[0].type == &t && changes[0].path == "hp");
  CHECK_EQ(changes[1].path, std::string("offset.x"));
  CHECK_EQ(Registry::Instance().PendingChanges(), size_t(0));
}

TEST_CASE("reflection: call functions by name") {
  BTestComponent c;
  const auto& t = BTestComponent::StaticType();
  std::string result, err;
  CHECK(Registry::Call(&c, t, "Heal", {"5"}, &result)); CHECK_EQ(result, std::string("105")); CHECK_EQ(c.hp, 105);
  CHECK(!Registry::Call(&c, t, "Heal", {}, &result, &err)); CHECK(err.find("expected 1 argument") != std::string::npos);
  CHECK(!Registry::Call(&c, t, "Heal", {"\"x\""}, &result, &err));
  CHECK(!Registry::Call(&c, t, "Fly", {}, &result, &err));
  BTestExtras x;
  const auto& tx = BTestExtras::StaticType();
  CHECK(Registry::Call(&x, tx, "Reset", {"2", "\"test\""}, &result)); CHECK_EQ(result, std::string("null"));
  CHECK(x.loot.size() == 2 && x.lastReason == "test");
  CHECK(Registry::Call(&x, tx, "Length", {}, &result)); CHECK_EQ(result, std::string("0"));
}

TEST_CASE("reflection: colors, arrays, nested structs, handles") {
  BTestExtras x;
  const auto& t = BTestExtras::StaticType();
  std::string v;
  CHECK(Registry::Set(&x, t, "tint", "\"#ff8000\""));
  CHECK(x.tint.r == 1.f && x.tint.b == 0.f && x.tint.a == 1.f && x.tint.g > 0.5f && x.tint.g < 0.51f);
  CHECK(Registry::Set(&x, t, "tint.a", "0.5")); CHECK_EQ(x.tint.a, 0.5f);
  CHECK(Registry::Set(&x, t, "precise", "0.125")); CHECK_EQ(x.precise, 0.125);
  CHECK(Registry::Get(&x, t, "rotation", v)); CHECK_EQ(v, std::string("[0,0,0,1]"));
  CHECK(Registry::Set(&x, t, "target", "42")); CHECK_EQ(x.target.value, uint64_t(42));
  CHECK(Registry::Get(&x, t, "loot", v)); CHECK_EQ(v, std::string("[3,1,4]"));
  CHECK(Registry::Get(&x, t, "loot[2]", v)); CHECK_EQ(v, std::string("4"));
  CHECK(Registry::Set(&x, t, "loot[0]", "8")); CHECK_EQ(x.loot[0], 8);
  CHECK(!Registry::Set(&x, t, "loot[3]", "1"));
  CHECK(Registry::Set(&x, t, "anchor.y", "2")); CHECK_EQ(x.anchor.y, 2.f);
  CHECK(Registry::Set(&x, t, "anchor", "{\"x\":5}")); CHECK(x.anchor.x == 5.f && x.anchor.y == 2.f);
  CHECK(Registry::Set(&x, t, "path", "[{\"x\":1,\"y\":2},{\"x\":3}]")); CHECK_EQ(x.path.size(), size_t(2));
  CHECK(Registry::Set(&x, t, "path[1].y", "7")); CHECK_EQ(x.path[1].y, 7.f);
  CHECK(Registry::Get(&x, t, "path", v)); CHECK_EQ(v, std::string("[{\"x\":1,\"y\":2},{\"x\":3,\"y\":7}]"));
  CHECK(!(t.FindField("cache")->flags & bdg::kSave)); // Transient
  std::string err;
  CHECK(!Registry::Set(&x, t, "anchor", "{\"x\":1,\"z\":3}", &err)); CHECK(x.anchor.x == 5.f); // rolled back
}

TEST_CASE("reflection: inheritance") {
  BTestDerived d;
  const auto& t = BTestDerived::StaticType();
  CHECK(t.base == &BTestComponent::StaticType());
  CHECK(t.IsA(BTestComponent::StaticType()) && !BTestComponent::StaticType().IsA(t));
  CHECK(t.FindField("hp") != nullptr && t.FindField("armor") != nullptr);
  CHECK(Registry::Set(&d, t, "hp", "3")); CHECK_EQ(d.hp, 3);
  std::string v;
  CHECK(Registry::Get(&d, t, "armor", v)); CHECK_EQ(v, std::string("12"));
  CHECK(Registry::Call(&d, t, "Heal", {"1"}, &v)); CHECK_EQ(v, std::string("4"));
}
