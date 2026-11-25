#include "bdg_test.h"
#include "Input/InputActions.h"
#include <cmath>

using namespace bdg;

namespace {
InputEvent KeyEv(Key k, bool down) {
  InputEvent e;
  e.type = down ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
  e.key = k;
  return e;
}
}

TEST_CASE("input: actions and contexts files, devices to actions, priorities, triggers (queue 6-4)") {
  std::vector<InputActionDef> actions;
  std::string err;
  CHECK(InputSystem::ParseActions("{\"action\":\"Move\",\"type\":\"vec2\"}\n{\"action\":\"Jump\",\"type\":\"button\"}\n"
                                  "{\"action\":\"Aim\"}\n{\"action\":\"Brake\"}\n{\"action\":\"Throttle\",\"type\":\"axis\"}\n",
                                  actions, &err));
  CHECK(actions.size() == 5 && actions[0].type == ActionType::Vec2 && actions[2].type == ActionType::Button && actions[4].type == ActionType::Axis);
  std::vector<InputActionDef> bad;
  CHECK(!InputSystem::ParseActions("{\"action\":\"X\",\"type\":\"quaternion\"}\n", bad, &err) && err.find("line 1") != std::string::npos);
  InputContext foot, car;
  CHECK(InputSystem::ParseContext("{\"context\":\"OnFoot\",\"priority\":0}\n{\"action\":\"Move\",\"key\":\"WASD\"}\n"
                                  "{\"action\":\"Move\",\"key\":\"Gamepad.LeftStick\",\"mods\":[{\"deadzone\":0.2}]}\n"
                                  "{\"action\":\"Jump\",\"key\":\"Space\"}\n{\"action\":\"Jump\",\"key\":\"Gamepad.South\"}\n"
                                  "{\"action\":\"Aim\",\"key\":\"Mouse.Right\",\"hold\":0.25}\n"
                                  "{\"action\":\"Throttle\",\"key\":\"Up\"}\n{\"action\":\"Throttle\",\"key\":\"Down\",\"invert\":true}\n",
                                  foot, &err));
  CHECK(foot.name == "OnFoot" && foot.bindings.size() == 7 && foot.bindings[1].deadzone == 0.2f && foot.bindings[4].hold == 0.25f);
  CHECK(foot.bindings[6].invertX);
  CHECK(InputSystem::ParseContext("{\"context\":\"Vehicle\",\"priority\":10,\"enabled\":false}\n{\"action\":\"Brake\",\"key\":\"Space\"}\n", car, &err));
  CHECK(!car.enabled && car.priority == 10);
  InputContext noHeader;
  CHECK(!InputSystem::ParseContext("{\"action\":\"Jump\",\"key\":\"Space\"}\n", noHeader, &err));

  InputSystem in; // a fresh one (not the editor's)
  in.Set(actions, {foot, car});
  const double dt = 1.0 / 60.0;
  in.Feed(KeyEv(Key::D, true));
  in.Feed(KeyEv(Key::W, true));
  in.Update(dt);
  const glm::vec3 m = in.State("Move")->value;
  CHECK(std::abs(glm::length(glm::vec2(m)) - 1.f) < 1e-5f && m.x > 0.7f && m.y > 0.7f); // diagonal stays length 1
  in.Feed(KeyEv(Key::Space, true));
  in.Update(dt);
  CHECK(in.State("Jump")->pressed && in.State("Jump")->down && in.State("Jump")->presses == 1);
  in.Update(dt);
  CHECK(!in.State("Jump")->pressed && in.State("Jump")->down); // held: pressed only on the first frame
  in.Feed(KeyEv(Key::Space, false));
  in.Update(dt);
  CHECK(in.State("Jump")->released && !in.State("Jump")->down && in.State("Jump")->releases == 1);
  // gamepad: deadzone on the stick, a face button
  GamepadState pad;
  pad.connected = true;
  pad.leftX = 0.1f;
  pad.buttons = 1u << (uint32_t)PadButton::South;
  in.Feed(KeyEv(Key::D, false));
  in.Feed(KeyEv(Key::W, false));
  in.Update(dt, &pad);
  CHECK(glm::length(in.State("Move")->value) == 0.f && in.State("Jump")->pressed); // 0.1 is inside the 0.2 deadzone
  pad.leftX = 1.f;
  pad.buttons = 0;
  in.Update(dt, &pad);
  CHECK(std::abs(in.State("Move")->value.x - 1.f) < 1e-5f);
  // axis: Up +1, Down -1 (inverted)
  in.Feed(KeyEv(Key::Down, true));
  in.Update(dt);
  CHECK(in.State("Throttle")->value.x == -1.f);
  in.Feed(KeyEv(Key::Down, false));
  // hold trigger: Mouse.Right counts after 0.25 s
  InputEvent rmb;
  rmb.type = InputEvent::Type::MouseDown;
  rmb.button = 3;
  in.Feed(rmb);
  int framesToAim = 0;
  for (int i = 0; i < 60 && !in.State("Aim")->down; ++i, ++framesToAim) in.Update(dt);
  CHECK(framesToAim >= 14 && framesToAim <= 17); // 0.25 s at 60 Hz
  // priorities: the Vehicle context takes Space from Jump
  CHECK(in.EnableContext("Vehicle", true) && !in.EnableContext("Boat", true));
  in.Feed(KeyEv(Key::Space, true));
  in.Update(dt);
  CHECK(in.State("Brake")->pressed && !in.State("Jump")->down);
  in.EnableContext("Vehicle", false);
  in.Update(dt);
  CHECK(in.State("Jump")->down && !in.State("Brake")->down);
  // mouse delta: up on screen is +y
  in.ReleaseAll();
  InputEvent mv;
  mv.type = InputEvent::Type::MouseMove;
  mv.x = 100;
  mv.y = 100;
  in.Feed(mv);
  mv.x = 110;
  mv.y = 95;
  in.Feed(mv);
  std::vector<InputActionDef> lookActions = {{"Look", ActionType::Vec2}};
  InputContext look;
  look.name = "Look";
  look.bindings.push_back({"Look", "Mouse.Delta"});
  in.Set(lookActions, {look});
  in.Update(dt);
  CHECK(in.State("Look")->value == glm::vec3(10, 5, 0));
  in.Update(dt);
  CHECK(in.State("Look")->value == glm::vec3(0)); // per frame
  // defaults when a project has no files
  in.UseDefaults();
  CHECK(in.Actions().size() == 5 && in.Contexts().size() == 1);
  CHECK(in.StateJson("Nope") == "null");
}
