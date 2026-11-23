#pragma once
#include "UI/UI.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace bdg {
// ---- Docking: split tree of tabbed panels (queue 1-1) ----------------------------------------
// A node is either a split (row = a left / b right, column = a top / b bottom, ratio = a's share) or a
// leaf (tab group: panel ids + selected tab). Panel contents are the Dock widget's children; a child's id
// is the panel id. Only the selected tab of each leaf is laid out, the rest are hidden.
struct DockRect {
  float x = 0, y = 0, w = 0, h = 0;
  bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct DockNode {
  bool split = false, alive = true;
  Axis axis = Axis::Row;
  float ratio = 0.5f;
  int a = -1, b = -1, parent = -1;
  std::vector<std::string> tabs;
  int sel = 0;
  DockRect rect; // computed by layout
};

enum class DockZone { None = -1, Center, Left, Right, Top, Bottom };

struct DockState {
  std::vector<DockNode> nodes;
  int root = -1;
  std::unordered_map<std::string, std::string> titles; // panel id -> tab label
  std::unordered_map<std::string, DockRect> tabRects;  // computed each layout
  std::string layoutPath;                              // changes are saved here ("" = not saved)

  // Interaction state (mouse coordinates in UI units).
  int dragSplit = -1, hoverSplit = -1;
  std::string pressTab;
  float pressX = 0, pressY = 0, mouseX = 0, mouseY = 0;
  bool tabDragging = false;
  int dropLeaf = -1;
  DockZone dropZone = DockZone::None;

  static constexpr float kGap = 1.f, kGrab = 4.f, kMinPane = 80.f;

  int AddLeaf(std::vector<std::string> tabs, int sel = 0);
  int AddSplit(Axis axis, float ratio, int a, int b);
  int FindLeaf(const std::string& panel) const;
  bool SelectPanel(const std::string& panel);           // shows the panel's tab in its leaf
  void MovePanel(const std::string& panel, int targetLeaf, DockZone zone);
  DockRect ZoneRect(int leaf, DockZone zone) const;     // drop preview area
  std::string Describe() const;                          // compact text form for logs and tests
  bool Save(const std::string& path) const;              // JSONL, one node per line, root first
  bool Load(const std::string& path);                    // false (state unchanged) if missing or invalid

private:
  void Replace(int oldNode, int newNode);                // puts newNode where oldNode hangs in the tree
  std::string DescribeNode(int n) const;
};

std::unique_ptr<Widget> MakeDock(std::string id, std::shared_ptr<DockState> state);
}
