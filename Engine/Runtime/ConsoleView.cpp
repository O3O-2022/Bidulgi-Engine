#include "Runtime/ConsoleView.h"
#include "Render2D/Batch2D.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <regex>

namespace bdg {
namespace {
constexpr size_t kNone = (size_t)-1;
Color WithAlpha(Color c, float a) { return {c.r, c.g, c.b, a}; }
std::string Lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int LevelIndex(LogLevel l) { return l == LogLevel::Info ? 0 : l == LogLevel::Warn ? 1 : 2; }
} // namespace

bool ConsoleView::FindSource(const std::string& text, std::string& name, int& line) {
  // "C:/x/Probe.cs:12", "Scripts/Player.cs(40,7)", "quest.lua:12:" -> the file and the line
  static const std::regex re(R"re(((?:[A-Za-z]:)?[^\s:()"'<>|*?]+\.(?:cs|cpp|h|hpp|c|lua|py|scene|prefab|json|txt|graph|ui|material|glsl|hlsl))(?::|\()(\d+))re");
  std::smatch m;
  if (!std::regex_search(text, m, re)) return false;
  name = m[1].str();
  line = std::atoi(m[2].str().c_str());
  return true;
}

void ConsoleView::Bind(UIContext& ui) {
  static const char* levels[] = {"info", "warn", "error"};
  for (int k = 0; k < 3; ++k)
    if (Widget* b = ui.Find(prefix + "." + levels[k])) {
      b->variant = 2; // flat; lit (selected) while that level is shown
      b->onClick = [this, k](Widget&) {
        show_[k] = !show_[k];
        Refilter();
      };
    }
  if (Widget* b = ui.Find(prefix + ".clear"))
    b->onClick = [this](Widget&) {
      clearedAt_ = lines_.empty() ? last_ : lines_.back().seq;
      selected_ = 0;
      Refilter();
    };
  if (Widget* b = ui.Find(prefix + ".follow")) {
    b->variant = 2;
    b->onClick = [this](Widget&) {
      follow_ = !follow_;
      if (follow_) scroll_ = MaxScroll();
    };
  }
}

void ConsoleView::Sync(UIContext& ui) {
  if (const uint64_t latest = LogLatestSeq(); latest != last_) {
    for (LogLine& l : LogHistory(last_))
      if (channel.empty() || l.channel == channel) lines_.push_back(std::move(l));
    last_ = latest;
    if (lines_.size() > kLogHistory) lines_.erase(lines_.begin(), lines_.begin() + (std::ptrdiff_t)(lines_.size() - kLogHistory));
    Refilter();
  }
  if (const Widget* f = ui.Find(prefix + ".search")) // the search narrows as one types (a field only commits on Enter)
    if (std::string want = Lower(f->text2); want != filter_) {
      filter_ = std::move(want);
      Refilter();
    }
  static const char* levels[] = {"info", "warn", "error"};
  static const char* names[] = {"정보", "경고", "오류"};
  for (int k = 0; k < 3; ++k)
    if (Widget* b = ui.Find(prefix + "." + levels[k])) {
      const std::string text = std::format("{} {}", names[k], counts_[k]);
      if (b->text != text) b->text = text;
      b->selected = show_[k];
    }
  if (Widget* b = ui.Find(prefix + ".follow")) b->selected = follow_;
}

void ConsoleView::Refilter() {
  shown_.clear();
  counts_[0] = counts_[1] = counts_[2] = 0;
  for (size_t i = 0; i < lines_.size(); ++i) {
    const LogLine& l = lines_[i];
    if (l.seq <= clearedAt_) continue;
    const int k = LevelIndex(l.level);
    ++counts_[k]; // the counts are of what 지우기 left, before the toggles and the search
    if (!show_[k]) continue;
    if (!filter_.empty() && Lower(l.text).find(filter_) == std::string::npos) continue;
    shown_.push_back(i);
  }
  if (follow_) scroll_ = MaxScroll();
  scroll_ = std::clamp(scroll_, 0.f, MaxScroll());
}

float ConsoleView::MaxScroll() const {
  const float rows = rowH_ > 0 ? listH_ / rowH_ : 0;
  return std::max(0.f, (float)shown_.size() - std::floor(rows));
}

size_t ConsoleView::RowAt(float y) const {
  if (rowH_ <= 0) return kNone;
  const float r = std::floor((y - listY_) / rowH_ + scroll_);
  return r >= 0 && r < (float)shown_.size() ? (size_t)r : kNone;
}

void ConsoleView::OpenLine(const LogLine& l) {
  std::string name;
  int line = 0;
  if (!FindSource(l.text, name, line)) return;
  const std::string path = resolve ? resolve(name) : name;
  if (path.empty()) {
    LogInfo("console: {} is not in the project", name);
    return;
  }
  if (open) open(path, line);
}

void ConsoleView::HandleEvents(std::vector<InputEvent>& events, UIContext& ui) {
  using T = InputEvent::Type;
  const Widget* panel = ui.Find(prefix);
  const Widget* list = ui.Find(prefix + ".list");
  listVisible_ = panel && list && !panel->hidden && !list->hidden && list->w > 2 && list->h > 2; // its tab is in front
  if (!listVisible_) return;
  listX_ = list->x, listY_ = list->y, listW_ = list->w, listH_ = list->h;
  auto inside = [&](const InputEvent& e) { return e.x >= listX_ && e.y >= listY_ && e.x < listX_ + listW_ && e.y < listY_ + listH_; };
  for (size_t i = 0; i < events.size();) {
    const InputEvent& e = events[i];
    bool used = false;
    if (ui.MenuOpen() && e.type != T::Text && e.type != T::KeyDown && e.type != T::KeyUp) { ++i; continue; } // an open list owns the mouse
    if (e.type == T::Wheel && inside(e)) {
      scroll_ = std::clamp(scroll_ - e.wheel * 3, 0.f, MaxScroll());
      follow_ = scroll_ >= MaxScroll() - 0.01f; // at the end: follow again
      used = true;
    } else if (e.type == T::MouseDown && inside(e) && (e.button == 1 || e.button == 3)) {
      const size_t row = RowAt(e.y);
      const uint64_t seq = row != kNone ? lines_[shown_[row]].seq : 0;
      selected_ = seq;
      if (e.button == 1 && seq) {
        const double now = Now();
        if (seq == lastClickSeq_ && now - lastClick_ < 0.45) OpenLine(lines_[shown_[row]]); // a double-click
        lastClick_ = now;
        lastClickSeq_ = seq;
      } else if (e.button == 3) {
        std::vector<UIContext::MenuItem> items;
        const std::string text = seq ? lines_[shown_[row]].text : std::string();
        items.push_back({"복사", [&ui, text] { ui.SetClipboard(text); }, seq != 0});
        items.push_back({"모두 복사", [this, &ui] {
                           std::string all;
                           for (size_t k : shown_) all += lines_[k].text + "\n";
                           ui.SetClipboard(all);
                         }, !shown_.empty()});
        ui.OpenMenu(e.x, e.y, std::move(items));
      }
      used = true;
    } else if (e.type == T::MouseUp && inside(e)) {
      used = true;
    }
    if (used) events.erase(events.begin() + (std::ptrdiff_t)i);
    else ++i;
  }
}

void ConsoleView::Draw(Widget& w, UIContext& ui) {
  const Theme& t = ui.Tokens();
  Batch2D& b = ui.Batch();
  listX_ = w.x, listY_ = w.y, listW_ = w.w, listH_ = w.h;
  listVisible_ = !w.hidden && w.w > 2 && w.h > 2;
  rowH_ = t.N("size-row", 22);
  if (follow_) scroll_ = MaxScroll();
  scroll_ = std::clamp(scroll_, 0.f, MaxScroll());
  b.Rect(w.x, w.y, w.w, w.h, t.C("sunk"));
  const float fs = t.FontSize("code", 12);
  if (shown_.empty()) {
    const std::string msg = lines_.empty() || (counts_[0] + counts_[1] + counts_[2] == 0) ? emptyText : "거른 결과가 없습니다";
    const float lfs = t.FontSize("label", 12);
    ui.Text(w.x + (w.w - ui.Measure(lfs, msg)) * 0.5f, w.y + w.h * 0.5f - lfs, lfs, t.C("fg-faint"), msg);
    return;
  }
  b.PushClip(w.x, w.y, w.w, w.h);
  const size_t first = (size_t)std::floor(scroll_);
  const float off = (scroll_ - std::floor(scroll_)) * rowH_;
  for (size_t r = first; r < shown_.size(); ++r) {
    const float y = w.y + (float)(r - first) * rowH_ - off;
    if (y > w.y + w.h) break;
    const LogLine& l = lines_[shown_[r]];
    const int k = LevelIndex(l.level);
    if (l.seq == selected_) b.Rect(w.x, y, w.w, rowH_, t.C("selection"));
    else if (k == 2) b.Rect(w.x, y, w.w, rowH_, WithAlpha(t.C("err"), 0.1f));
    const Color c = k == 2 ? t.C("err") : k == 1 ? t.C("warn") : t.C("fg-faint");
    b.Rect(w.x + 10, y + rowH_ * 0.5f - 3, 6, 6, c, t.N("radius-sm", 3));
    ui.Text(w.x + 24, y + (rowH_ - fs) * 0.5f - 1, fs, k == 0 ? t.C("fg-dim") : c, l.text, true);
  }
  b.PopClip();
  if (const float max = MaxScroll(); max > 0) { // scroll bar
    const float total = (float)shown_.size(), th = std::max(20.f, w.h * (w.h / rowH_) / total), ty = w.y + (w.h - th) * (scroll_ / max);
    b.Rect(w.x + w.w - 5, ty, 3, th, WithAlpha(t.C("fg-faint"), 0.6f), t.N("radius-xs", 2));
  }
}

std::vector<std::string> ConsoleView::ShownTexts() const {
  std::vector<std::string> out;
  for (size_t k : shown_) out.push_back(lines_[k].text);
  return out;
}

bool ConsoleView::RowRect(size_t shownIndex, float& x, float& y, float& w, float& h) const {
  if (shownIndex >= shown_.size() || !listVisible_) return false;
  x = listX_;
  y = listY_ + ((float)shownIndex - scroll_) * rowH_;
  w = listW_;
  h = rowH_;
  return y >= listY_ && y + h <= listY_ + listH_;
}
} // namespace bdg
