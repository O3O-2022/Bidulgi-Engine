// In-game UI (queue 7-3, runtime): layout of UICanvas / UIElement objects and button clicks (no GPU needed).
#include "GameUI/GameUI.h"
#include "GameUI/GameUIComponents.h"
#include "Scene/SceneFile.h"
#include "bdg_test.h"
#include <cmath>
#include <string>

using namespace bdg;

namespace {
bool Near(float a, float b) { return std::fabs(a - b) < 0.01f; }
}

TEST_CASE("game ui: anchors, stretch, nesting, scaling and order; buttons click (queue 7-3)") {
  const char* text = R"({"v":1,"scene":"UI"}
{"id":"hud","name":"HUD","UICanvas":{"referenceSize":[1000,500]}}
{"id":"tl","name":"score","parent":"hud","UIElement":{"kind":"Text","anchor":"TopLeft","position":[10,20],"size":[200,50],"text":"점수 0"}}
{"id":"br","name":"ammo","parent":"hud","UIElement":{"kind":"Text","anchor":"BottomRight","position":[-10,-20],"size":[100,40]}}
{"id":"c","name":"cross","parent":"hud","UIElement":{"kind":"Panel","anchor":"Center","size":[20,20]}}
{"id":"st","name":"frame","parent":"hud","UIElement":{"kind":"Panel","anchor":"Stretch","position":[5,5],"size":[5,5]}}
{"id":"btn","name":"restart","parent":"st","UIElement":{"kind":"Button","anchor":"Center","size":[100,40],"text":"다시"}}
{"id":"hid","name":"secret","parent":"hud","UIElement":{"kind":"Panel","visible":false}}
{"id":"hidkid","name":"inside","parent":"hid","UIElement":{"kind":"Text"}}
{"id":"top","name":"Top canvas","UICanvas":{"referenceSize":[1000,500],"order":5}}
{"id":"over","name":"over","parent":"top","UIElement":{"kind":"Button","anchor":"Center","size":[100,40]}}
)";
  Scene s;
  std::string err;
  CHECK(ReadSceneText(s, text, nullptr, nullptr, &err));
  GameUI ui;
  // the screen is twice the reference size: everything doubles
  const GameUI::Rect view{0, 0, 2000, 1000};
  ui.Layout(s, view);
  CHECK(ui.Elements() == 6); // the hidden panel and its child are left out
  GameUI::Rect r;
  CHECK(ui.ElementRect("tl", r) && Near(r.x, 20) && Near(r.y, 40) && Near(r.w, 400) && Near(r.h, 100));
  CHECK(ui.ElementRect("br", r) && Near(r.x + r.w, 2000 - 20) && Near(r.y + r.h, 1000 - 40));
  CHECK(ui.ElementRect("c", r) && Near(r.x + r.w / 2, 1000) && Near(r.y + r.h / 2, 500) && Near(r.w, 40));
  CHECK(ui.ElementRect("st", r) && Near(r.x, 10) && Near(r.y, 10) && Near(r.w, 2000 - 20) && Near(r.h, 1000 - 20));
  CHECK(ui.ElementRect("btn", r) && Near(r.x + r.w / 2, 1000) && Near(r.w, 200)); // inside its parent element
  { // the placement the UI designer turns dragged rects back with: parent rect, parent, canvas, scale
    bool found = false;
    for (const GameUI::Placed& p : ui.Placement())
      if (p.object == "btn") {
        found = true;
        CHECK(p.parent == "st" && p.canvas == "hud" && Near(p.parentRect.x, 10) && Near(p.parentRect.w, 2000 - 20) && Near(p.scale, 2));
      }
    CHECK(found && ui.Placement().size() == ui.Elements());
  }
  CHECK(!ui.ElementRect("hid", r) && !ui.ElementRect("hidkid", r));
  // a wider screen with matchHeight 1: the height decides the scale
  ui.Layout(s, {0, 0, 3000, 1000});
  CHECK(ui.ElementRect("tl", r) && Near(r.w, 400) && ui.ElementRect("br", r) && Near(r.x + r.w, 3000 - 20));

  // clicks: press and release on the same button; the canvas with the higher order is on top
  ui.Input(s, view, 1000, 500, true); // "over" (order 5) covers "btn" here
  ui.Input(s, view, 1000, 500, false);
  auto clicks = [&](const char* id) { return static_cast<UIElement*>(s.Find(id)->Find("UIElement")->data)->clicks; };
  CHECK(clicks("over") == 1 && clicks("btn") == 0 && ui.Clicks() == 1);
  static_cast<UIElement*>(s.Find("over")->Find("UIElement")->data)->visible = false;
  ui.Input(s, view, 1000, 500, true);
  CHECK(static_cast<UIElement*>(s.Find("btn")->Find("UIElement")->data)->pressed);
  ui.Input(s, view, 1500, 900, false); // released elsewhere: no click
  CHECK(clicks("btn") == 0);
  ui.Input(s, view, 1000, 500, true);
  ui.Input(s, view, 1000, 510, false);
  CHECK(clicks("btn") == 1);
  static_cast<UIElement*>(s.Find("btn")->Find("UIElement")->data)->interactable = false;
  ui.Input(s, view, 1000, 500, true);
  ui.Input(s, view, 1000, 500, false);
  CHECK(clicks("btn") == 1 && ui.Clicks() == 2);
}

TEST_CASE("game ui: toggle, slider, input field, scroll view, dropdown (round 7)") {
  const char* text = R"({"v":1,"scene":"UI"}
{"id":"hud","name":"HUD","UICanvas":{"referenceSize":[1000,500]}}
{"id":"tg","name":"sound","parent":"hud","UIElement":{"kind":"Toggle","position":[0,0],"size":[200,40],"value":1}}
{"id":"sl","name":"volume","parent":"hud","UIElement":{"kind":"Slider","position":[0,100],"size":[400,40],"value":0}}
{"id":"in","name":"name","parent":"hud","UIElement":{"kind":"InputField","position":[0,200],"size":[300,50],"placeholder":"Name"}}
{"id":"sv","name":"list","parent":"hud","UIElement":{"kind":"ScrollView","position":[500,0],"size":[300,200]}}
{"id":"row1","name":"row1","parent":"sv","UIElement":{"kind":"Button","position":[0,0],"size":[300,100]}}
{"id":"row5","name":"row5","parent":"sv","UIElement":{"kind":"Button","position":[0,400],"size":[300,100]}}
{"id":"dd","name":"quality","parent":"hud","UIElement":{"kind":"Dropdown","position":[0,300],"size":[300,50],"options":"Low|Mid|High","value":0}}
)";
  Scene s;
  std::string err;
  CHECK(ReadSceneText(s, text, nullptr, nullptr, &err));
  auto el = [&](const char* id) { return static_cast<UIElement*>(s.Find(id)->Find("UIElement")->data); };
  GameUI ui;
  const GameUI::Rect view{0, 0, 1000, 500}; // scale 1
  auto click = [&](float x, float y) {
    ui.Input(s, view, x, y, false);
    ui.Input(s, view, x, y, true);
    ui.Input(s, view, x, y, false);
  };
  click(100, 20); // the toggle flips off
  CHECK(el("tg")->value == 0.f && el("tg")->clicks == 1);
  ui.Input(s, view, 10, 120, true); // the slider follows a drag
  ui.Input(s, view, 300, 120, true);
  CHECK(Near(el("sl")->value, 0.75f));
  ui.Input(s, view, 900, 120, false); // let go past its end: clamped to 1
  CHECK(Near(el("sl")->value, 1.f));
  click(100, 220); // the input field takes the keyboard
  CHECK(ui.Focused() == "in");
  GameUI::Pointer p{100, 220, false};
  p.text = "Kim하나";
  ui.Input(s, view, p);
  p = {100, 220, false};
  p.backspaces = 1;
  ui.Input(s, view, p);
  CHECK(el("in")->text == "Kim하");
  p = {100, 220, false};
  p.enter = true;
  ui.Input(s, view, p);
  CHECK(ui.Focused().empty());
  // the scroll view: row5 (at 400..500 inside a 200 tall view) is cut away until the wheel scrolls to it
  GameUI::Rect r;
  ui.Layout(s, view);
  CHECK(ui.ElementRect("row5", r) && Near(r.y, 400));
  click(650, 450); // below the view: not row5
  CHECK(el("row5")->clicks == 0);
  p = {650, 100, false};
  p.wheel = -20; // far down: clamped to the content's end (500 - 200)
  ui.Input(s, view, p);
  CHECK(Near(el("sv")->scroll.y, 300));
  ui.Layout(s, view);
  CHECK(ui.ElementRect("row5", r) && Near(r.y, 100));
  click(650, 150);
  CHECK(el("row5")->clicks == 1);
  // the dropdown opens on a click, a row picks
  click(100, 320);
  GameUI::Rect row;
  CHECK(el("dd")->open && ui.DropdownRow("dd", 2, row) && Near(row.y, 300 + 50 * 3));
  click(row.x + 10, row.y + 10);
  CHECK(el("dd")->value == 2.f && !el("dd")->open && !ui.DropdownRow("dd", 2, row));
}
