#include "Runtime/Screens.h"
#include "Render2D/Batch2D.h"
#include "UI/Theme.h"

namespace bdg {
namespace {
Color Alpha(Color c, float a) { c.a = a; return c; }

std::unique_ptr<Widget> DimLabel(std::string text, float size = 0) {
  auto l = MakeLabel(std::move(text));
  l->variant = 1;
  l->fontSize = size;
  return l;
}

std::unique_ptr<Widget> Spacer() { auto s = MakeBox(Axis::Row); s->width = Length::Fill(); return s; }

// Scene viewport placeholder: grid, selected object with gizmo, camera label. Real 3D arrives in M3.
void DrawSceneView(Widget& w, UIContext& ui) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  b.Rect(w.x, w.y, w.w, w.h, t.C("sunk"));
  Color grid = Alpha(t.C("line"), 0.7f);
  for (float gx = w.x + 40; gx < w.x + w.w; gx += 40) b.Line(gx, w.y, gx, w.y + w.h, 1, grid);
  for (float gy = w.y + 40; gy < w.y + w.h; gy += 40) b.Line(w.x, gy, w.x + w.w, gy, 1, grid);
  float cx = w.x + w.w * 0.5f, cy = w.y + w.h * 0.55f;
  b.Rect(cx - 48, cy - 48, 96, 96, Alpha(t.C("prefab"), 0.85f), t.N("radius-lg", 5), 2, t.C("accent"));
  b.Line(cx, cy, cx + 86, cy, 3, t.C("axis-x"));
  b.Line(cx, cy, cx, cy - 86, 3, t.C("axis-y"));
  b.Line(cx, cy, cx - 52, cy + 46, 3, t.C("axis-z"));
  ui.Text(w.x + 10, w.y + 8, t.FontSize("overlay", 11), t.C("fg-dim"), "Perspective · Lit · 중사양");
  ui.Text(w.x + w.w - 92, w.y + 8, t.FontSize("overlay", 11), t.C("fg-dim"), "Draw 182 / 200");
}

// Project browser placeholder: asset tiles (thumbnails arrive with the asset system in M2).
void DrawProjectTiles(Widget& w, UIContext& ui) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  const char* names[] = {"Characters", "Weapon.cpp", "Player.cs", "quest.lua", "Door.graph", "Knight.prefab",
                         "M_Gold.mat", "forest.scene", "hit.wav", "Attack.anim", "AG_Knight", "HUD.widget"};
  float x = w.x + 12, y = w.y + 10, tile = 72, cap = t.FontSize("caption", 10.5f);
  for (int i = 0; i < 12; ++i) {
    if (x + tile > w.x + w.w) { x = w.x + 12; y += tile + 30; }
    if (y + tile > w.y + w.h) break;
    bool prefab = i == 5 || i == 10;
    b.Rect(x, y, tile, tile, prefab ? Alpha(t.C("prefab"), 0.35f) : t.C("raise"), t.N("radius-lg", 5), 1, t.C("line-strong"));
    ui.Text(x, y + tile + 6, cap, t.C("fg-dim"), names[i]);
    x += tile + 18;
  }
}

std::unique_ptr<Widget> ComponentHeader(std::string title) {
  auto row = MakeBox(Axis::Row, 8);
  row->padL = 10; row->padT = 6; row->padB = 6;
  row->crossAlign = Align::Center;
  auto& l = row->Add(MakeLabel(std::move(title)));
  l.fontSize = 12.5f;
  row->Add(Spacer());
  return row;
}
}

std::unique_ptr<Widget> BuildEditorScreen() {
  auto root = MakeBox(Axis::Column, 1);

  // Menu bar
  auto& menu = root->Add(MakeBox(Axis::Row, 22));
  menu.height = Length::Px(28);
  menu.padL = 12; menu.padR = 12;
  menu.crossAlign = Align::Center;
  for (const char* m : {"File", "Edit", "Assets", "GameObject", "Component", "Window", "Help"}) menu.Add(DimLabel(m));
  menu.Add(Spacer());
  menu.Add(DimLabel("비둘기 엔진 0.0.1", 12));

  // Workspace bar
  auto& ws = root->Add(MakeBox(Axis::Row, 8));
  ws.height = Length::Px(38);
  ws.padL = 8; ws.padR = 8;
  ws.crossAlign = Align::Center;
  auto& wsTabs = ws.Add(MakeTabs("ws.tabs", {"Level", "Graph", "Animation", "UI Designer", "Scripting"}, 0));
  wsTabs.variant = 1;
  ws.Add(Spacer());
  ws.Add(MakeButton("tool.play", "▶ Play", true));
  ws.Add(MakeButton("tool.pause", "Pause"));
  ws.Add(MakeButton("tool.step", "Step"));
  ws.Add(Spacer());
  ws.Add(MakeBadge("중사양"));
  ws.Add(MakeBadge("Windows"));

  // Main area
  auto& main = root->Add(MakeBox(Axis::Row, 1));
  main.height = Length::Fill();

  auto& hier = main.Add(MakePanelTabs("panel.hierarchy", {"Hierarchy"}));
  hier.width = Length::Px(260);
  hier.Add(MakeListRow("h.main", "Main", "Scene"));
  hier.Add(MakeListRow("h.camera", "Main Camera", "Camera", 1));
  hier.Add(MakeListRow("h.sun", "Sun", "Light", 1));
  hier.Add(MakeListRow("h.sky", "Sky", "", 1));
  hier.Add(MakeListRow("h.world", "World Settings", "", 1));
  hier.Add(MakeListRow("h.ground", "Ground", "", 1));
  hier.Add(MakeListRow("h.player", "Player", "Prefab", 1)).selected = true;
  hier.Add(MakeListRow("h.weapon", "Weapon", "", 2));
  hier.Add(MakeListRow("h.crystal", "Crystal", "Prefab", 1));

  auto& center = main.Add(MakeBox(Axis::Column, 1));
  center.width = Length::Fill();
  auto& scene = center.Add(MakePanelTabs("panel.scene", {"Scene", "Game"}));
  scene.height = Length::Fill();
  scene.padB = 1; // viewport fills the body
  auto& view = scene.Add(MakeCustom("scene.view", DrawSceneView));
  view.height = Length::Fill();
  auto& project = center.Add(MakePanelTabs("panel.project", {"Project", "Console"}));
  project.height = Length::Px(210);
  auto& tiles = project.Add(MakeCustom("project.tiles", DrawProjectTiles));
  tiles.height = Length::Fill();

  auto& insp = main.Add(MakePanelTabs("panel.inspector", {"Inspector", "Project Settings"}));
  insp.width = Length::Px(320);
  insp.gap = 1;
  auto& nameRow = insp.Add(MakeBox(Axis::Row, 8));
  nameRow.padL = 10; nameRow.padT = 4; nameRow.padB = 6;
  nameRow.crossAlign = Align::Center;
  nameRow.Add(MakeLabel("Player")).fontSize = 15;
  nameRow.Add(Spacer());
  nameRow.Add(MakeBadge("Prefab"));
  insp.Add(ComponentHeader("Transform"));
  insp.Add(MakeField("f.position", "Position", "0, 1, 0"));
  insp.Add(MakeField("f.rotation", "Rotation", "0, 90, 0"));
  insp.Add(MakeField("f.scale", "Scale", "1, 1, 1"));
  insp.Add(ComponentHeader("PlayerMove (C#)"));
  insp.Add(MakeField("f.speed", "Speed", "5.0"));
  insp.Add(MakeField("f.jump", "Jump Height", "4.5"));
  insp.Add(ComponentHeader("Health (C++)"));
  insp.Add(MakeField("f.health", "Max Health", "100"));
  auto& addRow = insp.Add(MakeBox(Axis::Row));
  addRow.padL = 10; addRow.padT = 10;
  addRow.crossAlign = Align::Center;
  addRow.Add(MakeButton("insp.add", "Add Component"));

  // Status bar
  auto& status = root->Add(MakeBox(Axis::Row, 8));
  status.height = Length::Px(24);
  status.padL = 10; status.padR = 10;
  status.crossAlign = Align::Center;
  status.Add(MakeBadge("Saved", 1));
  status.Add(DimLabel("tokens.json live · F1: 부품 갤러리", 10.5f));
  status.Add(Spacer());
  status.Add(DimLabel("60 fps · Draw 1", 10.5f));
  return root;
}

std::unique_ptr<Widget> BuildGalleryScreen(int* clickCounter) {
  auto root = MakeBox(Axis::Column, 16);
  root->Pad(16);
  auto& cover = root->Add(MakeCover("비둘기 엔진 디자인 시스템", "부품 8개: Panel · Tabs · Button · Field · ListRow · Badge · Node · Cover"));
  cover.height = Length::Px(112);

  auto& row = root->Add(MakeBox(Axis::Row, 16));
  row.height = Length::Fill();

  auto& hier = row.Add(MakePanel("panel.hierarchy", "Hierarchy"));
  hier.width = Length::Px(280);
  hier.Add(MakeListRow("row.camera", "Main Camera", "Camera"));
  hier.Add(MakeListRow("row.sun", "Sun", "Light"));
  hier.Add(MakeListRow("row.sky", "Sky"));
  hier.Add(MakeListRow("row.world", "World Settings"));
  hier.Add(MakeListRow("row.player", "Player", "Prefab")).selected = true;
  hier.Add(MakeListRow("row.weapon", "Weapon", "", 1));
  hier.Add(MakeListRow("row.crystal", "Crystal", "Prefab"));

  auto& insp = row.Add(MakePanel("panel.inspector", "Inspector"));
  insp.width = Length::Fill();
  insp.gap = 2;
  insp.Add(MakeField("field.position", "Position", "0, 1, 0"));
  insp.Add(MakeField("field.rotation", "Rotation", "0, 90, 0"));
  insp.Add(MakeField("field.scale", "Scale", "1, 1, 1"));
  insp.Add(MakeField("field.speed", "Speed", "5.0"));
  auto& buttons = insp.Add(MakeBox(Axis::Row, 8));
  buttons.padL = 12; buttons.padT = 10; buttons.padB = 6;
  buttons.crossAlign = Align::Center;
  auto& apply = buttons.Add(MakeButton("btn.apply", "Apply", true));
  apply.onClick = [clickCounter](Widget&) { ++*clickCounter; };
  buttons.Add(MakeButton("btn.revert", "Revert"));
  buttons.Add(MakeButton("btn.select", "Select Prefab"));
  auto& badges = insp.Add(MakeBox(Axis::Row, 6));
  badges.padL = 12; badges.padT = 6;
  badges.crossAlign = Align::Center;
  badges.Add(MakeBadge("C#"));
  badges.Add(MakeBadge("Saved", 1));
  badges.Add(MakeBadge("Prefab"));

  auto& side = row.Add(MakeBox(Axis::Column, 16));
  side.width = Length::Px(440);
  side.Add(MakeTabs("tabs.view", {"Scene", "Game", "Graph", "Animation"}, 2));
  auto& nodes = side.Add(MakeBox(Axis::Row, 28));
  nodes.crossAlign = Align::Start;
  nodes.padT = 8; nodes.padL = 4;
  nodes.Add(MakeNode("node.start", "Start", "Event", 1, {"out|exec|", "out|object|Self"}));
  nodes.Add(MakeNode("node.rotate", "Rotate", "Transform", 2, {"in|exec|", "in|object|Target", "in|float|Speed", "out|exec|"}));
  return root;
}
}
