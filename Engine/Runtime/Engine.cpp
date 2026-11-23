// C API hub implementation: every bdg_api.h function forwards into the C++ engine here.
#include "bdg/bdg_api.h"
#include "Core/Image.h"
#include "Core/Log.h"
#include "Platform/Window.h"
#include "RHI/RHI.h"
#include "Render2D/Batch2D.h"
#include "Runtime/Screens.h"
#include "Text/Font.h"
#include "UI/Theme.h"
#include "UI/UI.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <format>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <SDL3/SDL_platform.h>
#include <cctype>
#include <cstring>
#include <ctime>
#undef DrawText // windows.h macro would rename bdg::Font::DrawText in this file
#include <filesystem>
#include <new>

namespace {
// Looks for a file relative to the working folder and up to three parent folders
// (works from the repo root, Bin/ and build/<Config>/).
std::string FindUp(const char* envName, std::initializer_list<const char*> relPaths) {
  if (const char* env = std::getenv(envName)) return env;
  for (const char* rel : relPaths) {
    std::filesystem::path base = std::filesystem::current_path();
    for (int up = 0; up < 4; ++up, base = base.parent_path())
      if (std::filesystem::exists(base / rel)) return (base / rel).string();
  }
  return {};
}

std::string FindFont() {
  std::string p = FindUp("BDG_FONT", {"Assets/Fonts/Pretendard-Regular.otf"});
  if (!p.empty()) return p;
  for (const char* sys : {"C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/segoeui.ttf"}) // development fallback
    if (std::filesystem::exists(sys)) return sys;
  return {};
}

double ProcessCpuMs() { // user + kernel time of this process (idle check)
  FILETIME c{}, x{}, k{}, u{};
  if (!GetProcessTimes(GetCurrentProcess(), &c, &x, &k, &u)) return 0;
  auto ms = [](FILETIME f) { return (double)(((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime) / 10000.0; };
  return ms(k) + ms(u);
}

double RamMB() {
  PROCESS_MEMORY_COUNTERS pmc{};
  GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
  return pmc.WorkingSetSize / (1024.0 * 1024.0);
}

void DrawStress(bdg::Batch2D& b, float W, float H) { // queue 0-4 exit check: 10k rects in one draw call
  for (int i = 0; i < 10000; ++i) {
    uint32_t rgb = 0x2040a0 + (i * 2654435761u >> 8 & 0x3f3f3f);
    float x = (i % 125) * (W / 125.f), y = (i / 125) * (H / 80.f);
    b.Rect(x, y, W / 125.f - 2, H / 80.f - 2, {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1}, 3);
  }
}
}

struct bdg_engine {
  bdg::Window window;
  bdg::RHI rhi;
  bdg::Batch2D batch;
  bdg::Font font;
  bdg::Theme theme;
  std::unique_ptr<bdg::UIContext> editor, gallery;
  bool showGallery = false, selfTest = false, stress = false, hasFont = false;
  std::chrono::steady_clock::time_point createdAt;
  int clicks = 0;
  bool bugRequested = false;
  int32_t maxFrames = 0;
};

namespace {
// Scripted input for automated checks (BDG_UI_SELFTEST=1 with BDG_SCREEN=gallery).
void InjectSelfTest(bdg_engine* e, int frame, float scale) { // scale: UI units -> drawable pixels
  using T = bdg::InputEvent::Type;
  bdg::UIContext& ui = *e->gallery;
  auto key = [&](bdg::Key k, bool ctrl = false) { bdg::InputEvent ev{T::KeyDown}; ev.key = k; ev.ctrl = ctrl; e->window.InjectEvent(ev); };
  auto text = [&](T type, const char* s) { bdg::InputEvent ev{type}; ev.text = s; e->window.InjectEvent(ev); };
  auto click = [&](float x, float y) {
    x *= scale; y *= scale; // injected like real input (drawable pixels)
    e->window.InjectEvent({T::MouseMove, x, y});
    e->window.InjectEvent({T::MouseDown, x, y, 1});
    e->window.InjectEvent({T::MouseUp, x, y, 1});
  };
  if (frame == 2) { // click Apply, Tab twice, Enter
    bdg::Widget* btn = ui.Find("btn.apply");
    click(btn->x + btn->w * 0.5f, btn->y + btn->h * 0.5f);
    key(bdg::Key::Tab); key(bdg::Key::Tab); key(bdg::Key::Enter);
  }
  if (frame == 4) { // text input: select all, type Korean, backspace, undo, IME composition
    bdg::Widget* f = ui.Find("field.speed");
    click(f->x + f->w - 20, f->y + f->h * 0.5f);
    key(bdg::Key::A, true);
    text(T::Text, "한글");
    text(T::Text, " 입력");
    key(bdg::Key::Backspace);
    key(bdg::Key::Z, true);
    text(T::TextEditing, "ㅋ");
  }
}
}


// ---- Bug report (Ctrl+Shift+B or the status bar button) ----------------------------------
// Collects screenshot + log + system info into BugReports/<time>/ and opens a prefilled GitHub issue form.
// Nothing is uploaded automatically: the folder opens first so the user can review every file before attaching.
// User paths and the user name are replaced with %USERPROFILE% / %USERNAME%; reports older than 30 days are deleted.
std::string UrlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

namespace {
// Replaces every case-insensitive occurrence of `what` in `s` with `with`.
void ReplaceAllNoCase(std::string& s, const std::string& what, const std::string& with) {
  if (what.empty()) return;
  auto lower = [](std::string v) { for (char& c : v) c = (char)std::tolower((unsigned char)c); return v; };
  std::string ls = lower(s), lw = lower(what);
  std::string out;
  size_t pos = 0;
  for (size_t hit; (hit = ls.find(lw, pos)) != std::string::npos; pos = hit + what.size()) out += s.substr(pos, hit - pos) + with;
  s = out + s.substr(pos);
}
}

// Removes personal data from text that goes into a bug report (exported for the automated check).
std::string AnonymizeReportText(std::string s) {
  if (const char* home = std::getenv("USERPROFILE"); home && *home) {
    std::string h = home, fwd = h;
    for (char& c : fwd) if (c == '\\') c = '/';
    ReplaceAllNoCase(s, h, "%USERPROFILE%");
    ReplaceAllNoCase(s, fwd, "%USERPROFILE%");
  }
  if (const char* user = std::getenv("USERNAME"); user && std::strlen(user) >= 2) ReplaceAllNoCase(s, user, "%USERNAME%");
  return s;
}

std::string ReadTextFile(const std::filesystem::path& p) {
  std::string s;
  if (std::FILE* f = _wfopen(p.c_str(), L"rb")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) s.append(buf, n);
    std::fclose(f);
  }
  return s;
}

bool WriteBytes(const std::filesystem::path& p, const void* data, size_t size) {
  std::FILE* f = _wfopen(p.c_str(), L"wb");
  if (!f) return false;
  bool ok = std::fwrite(data, 1, size, f) == size;
  std::fclose(f);
  return ok;
}

void DeleteOldBugReports(const std::filesystem::path& root, int maxAgeDays) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * maxAgeDays);
  for (const fs::directory_entry& d : fs::directory_iterator(root, ec)) {
    if (!d.is_directory(ec)) continue;
    auto t = d.last_write_time(ec);
    if (!ec && t < cutoff) {
      fs::remove_all(d.path(), ec);
      bdg::LogInfo("bug report: deleted old report {}", d.path().filename().string());
    }
  }
}

void WriteBugReport(bdg_engine* e, int32_t w, int32_t h, float scale, bool openBrowser) {
  namespace fs = std::filesystem;
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &now);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
  fs::path root = fs::current_path() / "BugReports";
  DeleteOldBugReports(root, 30);
  fs::path dir = root / stamp;
  std::error_code ec;
  fs::create_directories(dir, ec);

  // Screenshot: full resolution compressed PNG; half resolution (2x2 box filter) if it would exceed 500 KB.
  std::vector<uint8_t> px, png;
  e->rhi.ReadPixels(w, h, px);
  bdg::EncodePNG(w, h, px.data(), true, png);
  if (png.size() > 500 * 1024) {
    int32_t hw = w / 2, hh = h / 2;
    std::vector<uint8_t> half((size_t)hw * hh * 4);
    for (int32_t y = 0; y < hh; ++y)
      for (int32_t x = 0; x < hw; ++x)
        for (int c = 0; c < 4; ++c) {
          auto at = [&](int32_t sx, int32_t sy) { return (int)px[((size_t)sy * w + sx) * 4 + c]; };
          half[((size_t)y * hw + x) * 4 + c] = (uint8_t)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1)) / 4);
        }
    bdg::EncodePNG(hw, hh, half.data(), true, png);
  }
  WriteBytes(dir / "screenshot.png", png.data(), png.size());

  std::string log = AnonymizeReportText(ReadTextFile("Bidulgi.log"));
  WriteBytes(dir / "Bidulgi.log", log.data(), log.size());

  std::string sys = AnonymizeReportText(std::format(
      "engine: Bidulgi Engine {}\nos: {}\ngpu: {}\nopengl: {}\ndrawable: {}x{}  ui_scale: {:.2f}\nram: {:.0f} MB\nscreen: {}\n",
      bdg_version_string(), SDL_GetPlatform(), e->rhi.gpuName, e->rhi.glVersion, w, h, scale, RamMB(),
      e->showGallery ? "gallery" : "editor"));
  WriteBytes(dir / "system.txt", sys.data(), sys.size());
  std::string readme =
      "Bidulgi Engine bug report\n\n"
      "Review these files before attaching them to the GitHub issue. Nothing was uploaded.\n"
      "  screenshot.png  the editor window when the report was made\n"
      "  Bidulgi.log     engine log of this session\n"
      "  system.txt      engine version, OS, GPU, screen size (already filled into the issue form)\n\n"
      "Your user folder and user name were replaced with %USERPROFILE% and %USERNAME%.\n"
      "Reports older than 30 days are deleted automatically.\n";
  WriteBytes(dir / "README.txt", readme.data(), readme.size());
  bdg::LogInfo("bug report: {} (screenshot {} KB)", AnonymizeReportText(dir.string()), (png.size() + 1023) / 1024);

  if (!openBrowser) return;
  e->window.OpenURL("file:///" + dir.generic_string()); // review first
  e->window.OpenURL("https://github.com/O3O-2022/Bidulgi-Engine/issues/new?template=bug_report.yml&title=" +
                    UrlEncode("[Bug] ") + "&system=" + UrlEncode(sys));
}

extern "C" {

bdg_result bdg_engine_create(const bdg_engine_desc* desc, bdg_engine** out) {
  if (!desc || !out) return BDG_ERR_INVALID_ARG;
  *out = nullptr;
  bdg::LogInfo("Bidulgi Engine {}", bdg_version_string());
  auto* e = new (std::nothrow) bdg_engine();
  if (!e) return BDG_ERR_PLATFORM;
  e->createdAt = std::chrono::steady_clock::now();
  e->maxFrames = desc->max_frames;
  e->stress = std::getenv("BDG_BATCH_STRESS") != nullptr;
  e->selfTest = std::getenv("BDG_UI_SELFTEST") != nullptr;
  const char* screen = std::getenv("BDG_SCREEN");
  e->showGallery = screen && std::string(screen) == "gallery";
  if (!e->window.Create(desc->title ? desc->title : "Bidulgi Engine", desc->width, desc->height)) { delete e; return BDG_ERR_PLATFORM; }
  if (!e->rhi.Init(e->window)) { e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }
  if (std::getenv("BDG_GL_ERROR_TEST")) e->rhi.DebugTriggerError(); // verifies GL debug output reaches the log
  if (!e->batch.Init(e->rhi)) { e->rhi.Shutdown(); e->window.Destroy(); delete e; return BDG_ERR_GRAPHICS; }
  std::string fontPath = FindFont();
  e->hasFont = !fontPath.empty() && e->font.Load(e->rhi, fontPath.c_str());
  if (e->hasFont) {
    e->batch.SetTextAtlas(e->font.Atlas());
    // Code font: bundled JetBrains Mono (OFL), then system fallbacks (not redistributed).
    std::string bundled = FindUp("BDG_FONT_MONO", {"Assets/Fonts/JetBrainsMono-Regular.ttf"});
    for (std::string mono : {bundled, std::string("C:/Windows/Fonts/consola.ttf"), std::string("C:/Windows/Fonts/cour.ttf")})
      if (!mono.empty() && std::filesystem::exists(mono) && e->font.LoadMono(mono.c_str())) break;
  }
  else bdg::LogWarn("no font found: text is not drawn (set BDG_FONT)");
  std::string tokens = FindUp("BDG_TOKENS", {"design-system/project/tokens.json"});
  if (tokens.empty() || !e->theme.Load(tokens)) bdg::LogWarn("design tokens not found: colors show as magenta (set BDG_TOKENS)");

  bdg::Font* font = e->hasFont ? &e->font : nullptr;
  e->editor = std::make_unique<bdg::UIContext>(e->batch, font, e->theme);
  e->editor->SetRoot(bdg::BuildEditorScreen());
  e->editor->SetWindow(&e->window);
  if (bdg::Widget* bug = e->editor->Find("status.bug")) bug->onClick = [e](bdg::Widget&) { e->bugRequested = true; };
  if (const char* tab = std::getenv("BDG_DOCK_TAB")) // 0 Project, 1 Console, 2 Build Log, 3 Profiler (screenshots/tests)
    if (bdg::Widget* dock = e->editor->Find("panel.bottom")) dock->selectedIndex = std::atoi(tab);
  e->gallery = std::make_unique<bdg::UIContext>(e->batch, font, e->theme);
  e->gallery->SetRoot(bdg::BuildGalleryScreen(&e->clicks));
  e->gallery->SetWindow(&e->window);
  *out = e;
  return BDG_OK;
}

bdg_result bdg_engine_run(bdg_engine* e) {
  if (!e) return BDG_ERR_INVALID_ARG;
  using clock = std::chrono::steady_clock;
  using msd = std::chrono::duration<double, std::milli>;
  // Interactive runs draw only when something changed (input, window event, Wake, token file edit, caret blink)
  // and otherwise sleep in the OS event wait. Automated runs (--frames N) draw every frame at full speed with
  // vsync off, so frame-indexed tests stay deterministic and perf numbers measure work, not the display.
  // BDG_IDLE_TEST=<seconds> runs the interactive loop for that long and logs idle CPU use and wake-up latency.
  const char* idleEnv = std::getenv("BDG_IDLE_TEST");
  const double idleTestSec = idleEnv ? std::max(0.5, std::atof(idleEnv)) : 0.0;
  const bool onDemand = e->maxFrames == 0 || idleTestSec > 0;
  if (!onDemand) e->window.SetSwapInterval(0);
  const double period = 1000.0 / e->window.RefreshRate();
  bool limiter = onDemand && e->window.SwapInterval() == 0; // no vsync: cap frames to the display rate by timer
  if (limiter) bdg::LogInfo("frame limiter: {:.0f} Hz", 1000.0 / period);
  int fastFrames = 0;     // consecutive frames much faster than the display: vsync is being ignored
  int pending = 2;        // frames still to draw (start-up, and after every change: 2 lets layout settle)
  bool idleShown = false; // status bar already says "idle"
  clock::time_point nextThemePoll = clock::now(), caretDue{};
  // Status bar statistics (only frames actually drawn while active; idle sleep is not counted).
  clock::time_point statStart = clock::now();
  int statFrames = 0;
  double statWorkMs = 0, lastWorkMs = 0;
  // Idle self-test state.
  clock::time_point idleEnd{}, idleStart{};
  double idleCpu0 = 0, wakeLatencyMs = -1;
  int idleFrames = 0;
  bool idleMeasuring = false;
  std::atomic<long long> wakeSentNs{0};
  std::thread waker;

  int32_t frames = 0;
  auto t0 = clock::now();
  if (idleTestSec > 0) idleEnd = t0 + std::chrono::microseconds((long long)(idleTestSec * 1e6));
  for (;;) {
    bdg::UIContext& active = e->showGallery ? *e->gallery : *e->editor;
    int32_t waitMs = 0;
    caretDue = {};
    if (onDemand && pending == 0 && !idleShown) { // about to sleep: say so once (one more frame)
      idleShown = true;
      pending = 1;
      if (bdg::Widget* perf = e->editor->Find("status.perf"))
        perf->text = std::format("idle  ·  {:.2f} ms  ·  RAM {:.0f} MB", lastWorkMs, RamMB());
    }
    if (onDemand && pending == 0) {
      auto now = clock::now();
      double wait = msd(nextThemePoll - now).count();
      if (int blink = active.NextBlinkMs(); blink >= 0 && blink < wait) {
        wait = blink;
        caretDue = now + std::chrono::milliseconds(blink);
      }
      if (idleTestSec > 0) wait = std::min(wait, msd(idleEnd - now).count());
      waitMs = (int32_t)std::clamp(std::ceil(wait), 1.0, 500.0);
      if (idleTestSec > 0 && !idleMeasuring) { // first sleep: start measuring, schedule a wake-up halfway
        idleMeasuring = true;
        idleStart = now;
        idleCpu0 = ProcessCpuMs();
        double half = msd(idleEnd - now).count() * 0.5;
        waker = std::thread([e, half, &wakeSentNs] {
          std::this_thread::sleep_for(std::chrono::microseconds((long long)(half * 1000)));
          wakeSentNs = clock::now().time_since_epoch().count();
          e->window.Wake();
        });
      }
    }
    if (!e->window.PumpEvents(waitMs)) break;
    auto now = clock::now();
    if (long long sent = wakeSentNs.load(); sent && wakeLatencyMs < 0)
      wakeLatencyMs = msd(now.time_since_epoch() - clock::duration(sent)).count();
    if (idleTestSec > 0 && now >= idleEnd) break;
    if (e->window.HadEvents()) {
      pending = std::max(pending, 2);
      if (idleShown) { idleShown = false; statStart = now; statFrames = 0; statWorkMs = 0; } // waking up
    }
    if (now >= nextThemePoll) {
      nextThemePoll = now + std::chrono::milliseconds(500);
      if (e->theme.ReloadIfChanged()) pending = std::max(pending, 2); // live token edits
    }
    if (caretDue != clock::time_point{} && now >= caretDue) pending = std::max(pending, 1);
    if (onDemand && pending == 0) continue; // timer wake-up with nothing to redraw
    if (pending > 0) --pending;

    for (const bdg::InputEvent& ev : e->window.Events())
    {
      if (ev.type == bdg::InputEvent::Type::KeyDown && ev.key == bdg::Key::F1) e->showGallery = !e->showGallery;
      // Bug report: Ctrl+Shift+B (F12 is reserved by Windows for the debugger break).
      if (ev.type == bdg::InputEvent::Type::KeyDown && ev.key == bdg::Key::B && ev.ctrl && ev.shift) e->bugRequested = true;
    }
    int32_t w = 0, h = 0;
    e->window.GetDrawableSize(w, h);
    // UI works in logical units: drawable pixels / display scale (150% Windows scaling -> 1.5).
    float scale = e->window.GetDisplayScale();
    if (const char* s = std::getenv("BDG_UI_SCALE")) scale = std::strtof(s, nullptr);
    if (scale < 0.5f) scale = 1.f;
    if (e->selfTest && e->showGallery) InjectSelfTest(e, frames, scale);
    float lw = w / scale, lh = h / scale;
    e->rhi.BeginFrame(w, h, e->theme.C("win"));
    e->batch.Begin((int32_t)lw, (int32_t)lh);
    bdg::UIContext& ui = e->showGallery ? *e->gallery : *e->editor;
    if (e->stress) DrawStress(e->batch, lw, lh);
    else {
      std::vector<bdg::InputEvent> events = e->window.Events();
      for (bdg::InputEvent& ev : events) { ev.x /= scale; ev.y /= scale; }
      ui.SetScale(scale);
      ui.HandleEvents(events);
      ui.Layout(lw, lh);
      ui.Draw();
    }
    e->batch.End();
    e->rhi.EndFrame();
    ++frames;
    if (idleMeasuring) ++idleFrames;

    bool last = e->maxFrames > 0 && frames >= e->maxFrames;
    if (last && std::getenv("BDG_BUG_TEST")) { // automated check (no browser): personal data must not reach the report
      const char* home = std::getenv("USERPROFILE");
      bdg::LogInfo("bug test: profile path {}\\Documents", home ? home : "-");
      e->bugRequested = true;
    }
    if (e->bugRequested) {
      e->bugRequested = false;
      WriteBugReport(e, w, h, scale, std::getenv("BDG_BUG_TEST") == nullptr);
    }
    if (last) {
      bdg::LogInfo("frame stats: quads={} flushes={} draws={} glyphs={} tokens={} ui_scale={:.2f} display_scale={:.2f} drawable={}x{}",
                   e->batch.QuadsThisFrame(), e->batch.FlushesThisFrame(), e->rhi.DrawCallsThisFrame(),
                   e->hasFont ? e->font.GlyphCount() : 0, e->theme.TokenCount(), scale, e->window.GetDisplayScale(), w, h);
      if (e->showGallery) {
        bdg::Widget* f = e->gallery->Focused();
        bdg::Widget* speed = e->gallery->Find("field.speed");
        bdg::LogInfo("ui: clicks(Apply)={} focused={} tabs.view={} field.speed=\"{}\" composing=\"{}\" cursor={}",
                     e->clicks, f ? f->id : "-", e->gallery->Find("tabs.view")->selectedIndex, speed->text2,
                     speed->composing, speed->cursor);
      }
      if (const char* dump = std::getenv("BDG_DUMP_WIDGETS"))
        bdg::LogInfo("widget dump {}: {}", ui.DumpJSONL(dump) ? "saved" : "FAILED", dump);
      if (const char* shot = std::getenv("BDG_SCREENSHOT")) {
        std::vector<uint8_t> px;
        e->rhi.ReadPixels(w, h, px);
        bool ok = bdg::WritePNG(shot, w, h, px.data(), true);
        bdg::LogInfo("screenshot {}: {}", ok ? "saved" : "FAILED", shot);
      }
    }
    double workMs = msd(clock::now() - now).count(); // CPU cost of the frame, without the vsync wait
    e->window.Present();
    double frameMs = msd(clock::now() - now).count();
    if (onDemand && !limiter && frameMs < period * 0.5) { // driver forces vsync off (e.g. a control panel setting)
      if (++fastFrames >= 30) {
        limiter = true;
        bdg::LogWarn("vsync: swap interval {} is ignored by the driver, frame limiter at {:.0f} Hz",
                     e->window.SwapInterval(), 1000.0 / period);
      }
    } else fastFrames = 0;
    if (limiter && frameMs < period) bdg::Window::SleepPrecise(period - frameMs);

    // Live status bar + profiler feed.
    lastWorkMs = workMs;
    if (frames > 1) bdg::PushFrameTime((float)workMs);
    ++statFrames;
    statWorkMs += workMs;
    if (double sec = std::chrono::duration<double>(clock::now() - statStart).count(); sec >= 0.5) {
      if (bdg::Widget* perf = e->editor->Find("status.perf"))
        perf->text = std::format("{:.0f} fps  ·  {:.2f} ms  ·  RAM {:.0f} MB", statFrames / sec, statWorkMs / statFrames, RamMB());
      statStart = clock::now();
      statFrames = 0;
      statWorkMs = 0;
    }
    if (frames == 1) { // startup = engine create -> first frame on screen (target: < 1000 ms)
      double ms = msd(clock::now() - e->createdAt).count();
      bdg::LogInfo("perf: startup_to_first_frame_ms={:.0f} (target < 1000)", ms);
    }
    if (last) break;
  }
  if (waker.joinable()) waker.join();
  double sec = std::chrono::duration<double>(clock::now() - t0).count();
  bdg::LogInfo("frames: {} in {:.2f}s", frames, sec);
  if (!onDemand && frames > 1)
    bdg::LogInfo("perf: avg_frame_ms={:.3f} (target < 16.7 at 60 fps; editor idle UI)", sec * 1000.0 / frames);
  if (idleMeasuring) {
    double idleSec = std::chrono::duration<double>(clock::now() - idleStart).count();
    double cpu = ProcessCpuMs() - idleCpu0;
    bdg::LogInfo("idle: seconds={:.2f} frames={} cpu_ms={:.0f} cpu_pct={:.2f} wake_latency_ms={:.2f} vsync={} limiter={}",
                 idleSec, idleFrames, cpu, cpu / (idleSec * 10.0), wakeLatencyMs, e->window.SwapInterval(), limiter ? 1 : 0);
  }
  return BDG_OK;
}

void bdg_engine_destroy(bdg_engine* e) {
  if (!e) return;
  e->editor.reset();
  e->gallery.reset();
  e->font.Shutdown();
  e->batch.Shutdown();
  e->rhi.Shutdown();
  e->window.Destroy();
  delete e;
  bdg::LogInfo("shutdown ok");
}

const char* bdg_result_string(bdg_result r) {
  switch (r) {
    case BDG_OK: return "ok";
    case BDG_ERR_PLATFORM: return "platform error";
    case BDG_ERR_GRAPHICS: return "graphics error";
    case BDG_ERR_INVALID_ARG: return "invalid argument";
  }
  return "unknown";
}

const char* bdg_version_string(void) { return "0.0.1"; }

} // extern "C"
