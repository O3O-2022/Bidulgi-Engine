// Handle lifetime tests (queue 1-4 exit criterion).
#include "Core/Command.h"
#include "Core/Handle.h"
#include "Scene/Components.h"
#include "bdg_test.h"

using namespace bdg;

namespace {
struct Thing { int value = 0; explicit Thing(int v = 0) : value(v) {} };
}

TEST_CASE("handles: create, destroy, reuse") {
  Pool<Thing> pool;
  Handle<Thing> a = pool.Create(1), b = pool.Create(2);
  CHECK(pool.IsValid(a) && pool.IsValid(b));
  CHECK_EQ(pool.Get(a)->value, 1);
  CHECK_EQ(pool.Count(), size_t(2));
  Thing* pa = pool.Get(a);
  pool.Create(3); pool.Create(4); pool.Create(5); // growth never moves live objects
  CHECK(pool.Get(a) == pa);

  CHECK(pool.Destroy(a));
  CHECK(!pool.IsValid(a));
  CHECK(pool.Get(a) == nullptr);
  CHECK(!pool.Destroy(a));                         // twice: refused
  Handle<Thing> c = pool.Create(9);                 // reuses a's slot ...
  CHECK_EQ(c.index, a.index);
  CHECK(c.generation != a.generation);              // ... with a new generation
  CHECK(!pool.IsValid(a) && pool.IsValid(c));       // the old handle stays dead
  CHECK_EQ(pool.Get(c)->value, 9);
  CHECK(pool.IsValid(b) && pool.Get(b)->value == 2);

  Handle<Thing> null;
  CHECK(null.IsNull() && !pool.IsValid(null));
  CHECK(Handle<Thing>::FromBits(c.Bits()) == c);
  Handle<Thing> far{99, 1};
  CHECK(!pool.IsValid(far));
}

TEST_CASE("handles: one error text for dead handles") {
  Pool<Thing> pool;
  Handle<Thing> h = pool.Create();
  pool.Destroy(h);
  std::string err;
  CHECK(pool.Resolve(h, "Thing", &err) == nullptr);
  CHECK_EQ(err, DeadHandleMessage("Thing", h.index, h.generation));
  CHECK(err.find("no longer valid") != std::string::npos);
}

TEST_CASE("handles: scene objects keep their id, not their handle") {
  Scene s;
  s.Create("g1", "Box");
  s.AddComponent("g1", Transform::StaticType());
  Handle<SceneObject> h = s.HandleOf("g1");
  CHECK(s.IsValid(h) && s.Resolve(h)->name == "Box");
  CommandBus bus(s);
  Command del;
  del.name = "del";
  del.target = "g1";
  CHECK(bus.Execute(del));
  std::string err;
  CHECK(!s.IsValid(h));
  CHECK(s.Resolve(h, &err) == nullptr && err.find("SceneObject handle") != std::string::npos);
  CHECK(bus.Undo());                                // the object comes back with the same id ...
  CHECK(s.Find("g1") != nullptr);
  CHECK(!s.IsValid(h));                             // ... but old handles do not come back to life
  Handle<SceneObject> h2 = s.HandleOf("g1");
  CHECK(s.IsValid(h2) && !(h2 == h));
  CHECK(s.HandleOf("g99").IsNull());
}
