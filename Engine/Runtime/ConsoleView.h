#pragma once
// The Console and Build Log panels (UI review B3 / U4): the real log, not sample lines.
//
//   - Lines come from the log history (Core/Log: the last 2000, any thread). The Build Log shows the lines written
//     while building (channel "build"); the Console shows all.
//   - Toolbar (widgets made in Screens.cpp, ids "<prefix>.*"): search box, 정보 / 경고 / 오류 toggles with their
//     counts, 지우기 (hides what is there now), 자동 스크롤 (follows new lines; scrolling up turns it off, scrolling
//     to the end turns it on).
//   - List ("<prefix>.list"): one row per line (size-row high, code type), errors on a faint red band, a scroll bar.
//     Wheel scrolls, a click selects, a double-click on a line naming "file.ext:line" opens that file, right-click:
//     복사 / 모두 복사 (the shown lines).
#include "Core/Log.h"
#include "Platform/Window.h"
#include <functional>
#include <string>
#include <vector>

namespace bdg {
class UIContext;
struct Widget;

class ConsoleView {
public:
  std::string prefix;  // widget ids: "console" / "buildlog"
  std::string channel; // "" = every line, else only that channel's
  std::string emptyText = "로그가 없습니다";
  // Opens a file at a line (double-click). Set by the editor; it decides how (the system's editor, or a log line in
  // automated runs).
  std::function<void(const std::string& path, int line)> open;
  // Turns the name found in a line into a file on disk ("" = not found): absolute, or under the project.
  std::function<std::string(const std::string& name)> resolve;

  void Bind(UIContext& ui);                 // the toolbar's buttons and search box
  void Sync(UIContext& ui);                 // new log lines; the toolbar's counts and states
  void HandleEvents(std::vector<InputEvent>& events, UIContext& ui); // the list's mouse (before the UI's own)
  void Draw(Widget& list, UIContext& ui);

  // Test hooks
  std::vector<std::string> ShownTexts() const;
  bool RowRect(size_t shownIndex, float& x, float& y, float& w, float& h) const; // of the last Draw
  bool Following() const { return follow_; }
  float Scroll() const { return scroll_; }
  static bool FindSource(const std::string& text, std::string& name, int& line); // "Scripts/A.cs:12" in a line

private:
  std::vector<LogLine> lines_; // this view's channel, oldest first
  uint64_t last_ = 0, clearedAt_ = 0, selected_ = 0, lastClickSeq_ = 0;
  double lastClick_ = 0;
  bool show_[3] = {true, true, true}; // info, warn, error
  bool follow_ = true;
  std::string filter_;
  std::vector<size_t> shown_; // indices into lines_ after the toggles, the search and 지우기
  float scroll_ = 0;          // first shown row (fractional while scrolling)
  float listX_ = 0, listY_ = 0, listW_ = 0, listH_ = 0, rowH_ = 22;
  bool listVisible_ = false;
  int counts_[3] = {0, 0, 0};

  void Refilter();
  float MaxScroll() const;
  size_t RowAt(float y) const; // index into shown_, or npos
  void OpenLine(const LogLine& l);
};
} // namespace bdg
