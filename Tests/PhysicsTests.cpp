#include "bdg_test.h"
#include "Physics/Physics.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include <cmath>
#include <cstring>

using namespace bdg;

namespace {
// A box dropped on a static floor; returns its height after `steps` steps of 1/60 s.
float DropBox(phys::Backend& be, bool twoD, int steps, uint64_t* hash = nullptr) {
  phys::BodyDesc floor;
  floor.user = 1;
  floor.motion = phys::Motion::Static;
  floor.shape.kind = twoD ? phys::ShapeKind::Box2D : phys::ShapeKind::Box;
  floor.shape.halfExtents = {10, 0.5f, 10};
  floor.position = {0, -0.5f, 0};
  phys::BodyDesc box;
  box.user = 2;
  box.shape.kind = twoD ? phys::ShapeKind::Box2D : phys::ShapeKind::Box;
  box.shape.halfExtents = {0.5f, 0.5f, 0.5f};
  box.position = {0, 3, 0};
  box.rotation = glm::angleAxis(0.3f, glm::vec3(0, 0, 1)); // lands on an edge and settles
  CHECK(be.Add(floor) != phys::kNoBody);
  const phys::BodyId id = be.Add(box);
  CHECK(id != phys::kNoBody);
  uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < steps; ++i) {
    be.Step(1.f / 60.f);
    const phys::BodyState s = be.Get(id);
    for (const float* f : {&s.position.x, &s.position.y, &s.position.z, &s.rotation.x, &s.rotation.w}) {
      uint32_t bits;
      std::memcpy(&bits, f, 4);
      h = (h ^ bits) * 1099511628211ull;
    }
  }
  if (hash) *hash = h;
  return be.Get(id).position.y;
}
}

TEST_CASE("physics: Jolt and Box2D behind the PHI — falls, rests, events, same run twice (queue 6-2)") {
  for (bool twoD : {false, true}) {
    auto a = twoD ? phys::CreateBox2D() : phys::CreateJolt();
    auto b = twoD ? phys::CreateBox2D() : phys::CreateJolt();
    CHECK(a->Caps().is3D == !twoD && a->Caps().deterministic);
    uint64_t ha = 0, hb = 0;
    const float ya = DropBox(*a, twoD, 240, &ha), yb = DropBox(*b, twoD, 240, &hb);
    CHECK(std::abs(ya - 0.5f) < 0.05f); // resting on the floor
    CHECK(ha == hb && ya == yb);        // deterministic: the same run gives the same bits
    CHECK(a->Bodies() == 2);
  }
  // a trigger reports what falls through it, and does not stop it
  auto be = phys::CreateJolt();
  phys::BodyDesc trigger;
  trigger.user = 7;
  trigger.motion = phys::Motion::Static;
  trigger.trigger = true;
  trigger.shape.halfExtents = {1, 0.25f, 1};
  trigger.position = {0, 1, 0};
  phys::BodyDesc ball;
  ball.user = 8;
  ball.shape.kind = phys::ShapeKind::Sphere;
  ball.shape.radius = 0.25f;
  ball.position = {0, 3, 0};
  be->Add(trigger);
  const phys::BodyId bid = be->Add(ball);
  int enter = 0, exit = 0;
  for (int i = 0; i < 90; ++i) {
    be->Step(1.f / 60.f);
    for (const phys::Contact& c : be->Contacts())
      if (c.trigger && ((c.a == 7 && c.b == 8) || (c.a == 8 && c.b == 7))) (c.begin ? enter : exit)++;
  }
  CHECK(enter == 1 && exit == 1);
  CHECK(be->Get(bid).position.y < 0.5f);
  // impulses and velocity
  const glm::vec3 v0 = be->Get(bid).velocity;
  be->Push(bid, {}, {}, {0, 10, 0}); // 10 N s on the default 1 kg: +10 m/s
  be->Step(1.f / 60.f);
  CHECK(be->Get(bid).velocity.y > v0.y + 9.f);
  // a moving body cannot be a triangle mesh
  phys::BodyDesc bad;
  bad.shape.kind = phys::ShapeKind::Mesh;
  bad.shape.points = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
  bad.shape.indices = {0, 1, 2};
  std::string err;
  CHECK(be->Add(bad, &err) == phys::kNoBody && !err.empty());
}

TEST_CASE("physics: Add Physics picks the body type and skips what is there (queue 6-2)") {
  Scene s;
  s.Create("a", "Crate");
  s.AddComponent("a", MeshRenderer::StaticType(), R"({"mesh":"Cube"})");
  s.Create("b", "Floor");
  s.AddComponent("b", Blockout::StaticType());
  s.Create("c", "Sprite");
  s.AddComponent("c", Rigidbody2D::StaticType());
  auto ca = PhysicsWorld::AddPhysicsCommands(s, "a"), cb = PhysicsWorld::AddPhysicsCommands(s, "b"), cc = PhysicsWorld::AddPhysicsCommands(s, "c");
  CHECK(ca.size() == 2 && ca[0].args.find("\"Rigidbody\"") != std::string::npos && ca[0].args.find("Static") == std::string::npos);
  CHECK(cb.size() == 2 && cb[0].args.find("Static") != std::string::npos); // greybox: level geometry
  CHECK(cc.size() == 1 && cc[0].args.find("Collider2D") != std::string::npos); // 2D object: the 2D collider only
}
