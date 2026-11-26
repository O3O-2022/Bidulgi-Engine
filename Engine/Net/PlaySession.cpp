#include "Net/PlaySession.h"
#include "Core/Log.h"
#include "Core/Utf8Path.h"
#include "Runtime/EditorModel.h"
#include "Runtime/PlayMode.h"
#include "Scene/SceneFile.h"
#include "UI/UI.h"
#include <algorithm>
#include <filesystem>
#include <format>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bdg {
namespace fs = std::filesystem;
namespace {
std::string U(const fs::path& p) {
  const std::u8string s = p.generic_u8string();
  return std::string(s.begin(), s.end());
}
fs::path PlayerExe() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH * 4];
  const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
  return fs::path(std::wstring(buf, n)).parent_path() / "BidulgiPlayer.exe";
#else
  return "BidulgiPlayer";
#endif
}
std::wstring Quote(const std::wstring& s) {
  if (!s.empty() && s.find_first_of(L" \t\"") == std::wstring::npos) return s;
  std::wstring o = L"\"";
  for (wchar_t c : s) {
    if (c == L'"') o += L'\\';
    o += c;
  }
  return o + L"\"";
}
} // namespace

const char* ConditionsName(int p) {
  static const char* names[] = {"Off", "LAN", "Internet", "Bad"};
  return names[std::clamp(p, 0, 3)];
}
void ApplyConditions(PlayOptions& o, int p) {
  o.conditions = std::clamp(p, 0, 3);
  static const float table[4][3] = {{0, 0, 0}, {10, 2, 0}, {100, 20, 2}, {250, 60, 10}}; // latency ms, jitter ms, loss %
  o.latencyMs = table[o.conditions][0];
  o.jitterMs = table[o.conditions][1];
  o.lossPercent = table[o.conditions][2];
}

PlaySession& PlaySession::Instance() {
  static PlaySession s;
  return s;
}

std::string PlaySession::Summary() const {
  const NetMode mode = options.mode == NetMode::Standalone && options.players > 1 ? NetMode::ListenServer : options.mode;
  std::string s = std::format("{} · {}P", NetModeName(mode), options.players);
  if (options.conditions) s += std::string(" · ") + ConditionsName(options.conditions);
  return s;
}

void PlaySession::OpenMenu(UIContext& ui, const Widget& b) {
  std::vector<UIContext::MenuItem> items;
  for (NetMode m : {NetMode::Standalone, NetMode::ListenServer, NetMode::Client})
    items.push_back({std::string("Net Mode: ") + NetModeName(m), [this, m] { options.mode = m; LogInfo("play options: {}", Summary()); }, true, options.mode == m});
  for (int n = 1; n <= 4; ++n)
    items.push_back({std::format("Players: {}", n), [this, n] { options.players = n; LogInfo("play options: {}", Summary()); }, true, options.players == n});
  for (int c = 0; c <= 3; ++c)
    items.push_back({std::string("Network: ") + ConditionsName(c), [this, c] { ApplyConditions(options, c); LogInfo("play options: {}", Summary()); }, true,
                     options.conditions == c});
  ui.OpenMenu(b.x, b.y + b.h + 2, std::move(items));
}

void PlaySession::BeforePlay(PlayMode& play, const EditorModel& model) {
  Stop();
  clients_ = 0;
  NetSettings s;
  s.sim.latencyMs = options.latencyMs;
  s.sim.jitterMs = options.jitterMs;
  s.sim.lossPercent = options.lossPercent;
  s.timeoutMs = options.timeoutMs;
  NetMode mode = options.mode;
  if (mode == NetMode::Standalone && options.players > 1) mode = NetMode::ListenServer;
  s.mode = mode;
  s.port = options.port;
  if (mode == NetMode::ListenServer) {
    s.address = "";
    s.playerName = "Host";
  } else if (mode == NetMode::Client) { // a dedicated server process with the edited scene, the editor joins it
    std::error_code ec;
    const fs::path dir = (options.projectDir.empty() ? fs::current_path(ec) : PathU8(options.projectDir)) / ".cache" / "net";
    fs::create_directories(dir, ec);
    const fs::path scene = dir / "play.scene";
    std::string err;
    if (!SaveTextFile(U(scene), WriteSceneText(model.scene, model.sceneName), &err)) LogError("play: {}", err);
    port_ = options.port ? options.port : (uint16_t)(40000 + GetCurrentProcessId() % 20000);
    Launch("Server", {"--server", "--scene", U(scene), "--port", std::to_string(port_)});
    s.address = "127.0.0.1";
    s.port = port_;
    s.playerName = "Editor";
  }
  play.netSettings = s;
}

void PlaySession::AfterPlay(PlayMode& play) {
  const NetMode mode = play.netSettings.mode;
  if (mode == NetMode::Standalone) return;
  if (mode != NetMode::Client) port_ = play.Net().Port();
  for (int i = 1; i < options.players; ++i) LaunchClient(std::format("Client {}", ++clients_));
}

int PlaySession::LaunchClient(const std::string& title, const std::vector<std::string>& extra) {
  std::vector<std::string> args = {"--connect", std::format("127.0.0.1:{}", port_), "--name", title};
  args.insert(args.end(), extra.begin(), extra.end());
  return Launch(title, args) ? (int)procs_.size() - 1 : -1;
}

bool PlaySession::Launch(const std::string& title, std::vector<std::string> args) {
#ifdef _WIN32
  std::error_code ec;
  const fs::path project = options.projectDir.empty() ? fs::current_path(ec) : PathU8(options.projectDir);
  const fs::path log = project / ".cache" / "net" / (title + ".log");
  fs::create_directories(log.parent_path(), ec);
  fs::remove(log, ec);
  args.insert(args.begin(), {"--project", U(project), "--title", title, "--log", U(log)});
  if (options.latencyMs > 0) args.insert(args.end(), {"--sim-latency", std::format("{}", options.latencyMs)});
  if (options.jitterMs > 0) args.insert(args.end(), {"--sim-jitter", std::format("{}", options.jitterMs)});
  if (options.lossPercent > 0) args.insert(args.end(), {"--sim-loss", std::format("{}", options.lossPercent)});
  if (options.timeoutMs != 5000) args.insert(args.end(), {"--net-timeout", std::format("{}", options.timeoutMs / 1000.0)});
  args.insert(args.end(), options.extraArgs.begin(), options.extraArgs.end());
  const fs::path exe = PlayerExe();
  std::wstring cmd = Quote(exe.wstring());
  for (const std::string& a : args) cmd += L" " + Quote(PathU8(a).wstring());
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!job_) { // every player this editor starts lives in one job that dies with the editor (closed or crashed)
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job_) SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof limits);
  }
  // started suspended so it is in the job before it runs a single instruction
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, exe.parent_path().c_str(), &si, &pi)) {
    LogError("play: cannot start {} ({}): error {}", title, U(exe), GetLastError());
    return false;
  }
  if (job_ && !AssignProcessToJobObject(job_, pi.hProcess)) LogWarn("play: {} is not tied to the editor (error {})", title, GetLastError());
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  procs_.push_back({title, U(log), pi.hProcess});
  LogInfo("play: started {} (process {})", title, pi.dwProcessId);
  return true;
#else
  (void)title;
  (void)args;
  return false;
#endif
}

bool PlaySession::Running(size_t i) const {
#ifdef _WIN32
  return i < procs_.size() && procs_[i].handle && WaitForSingleObject(procs_[i].handle, 0) == WAIT_TIMEOUT;
#else
  (void)i;
  return false;
#endif
}

void PlaySession::Stop() {
#ifdef _WIN32
  for (Process& p : procs_) {
    if (!p.handle) continue;
    if (WaitForSingleObject(p.handle, 0) == WAIT_TIMEOUT) {
      TerminateProcess(p.handle, 0);
      WaitForSingleObject(p.handle, 2000);
      LogInfo("play: closed {}", p.title);
    }
    CloseHandle(p.handle);
    p.handle = nullptr;
  }
#endif
  procs_.clear();
}

} // namespace bdg
