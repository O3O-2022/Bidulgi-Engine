#pragma once
// The editor's multiplayer Play options (round 7, M-5), like Unreal's "Play" drop-down: Net Mode, Number of Players and
// simulated network conditions. Play then runs the editor's game in that mode and opens the other players as game
// processes (the player exe with --project and --connect, window titles "Server" / "Client 1" ...); Stop closes them.
//
//   Standalone      the editor alone (players > 1: the editor hosts as a listen server anyway)
//   Listen Server   the editor hosts and plays; players - 1 client processes join it
//   Client          a dedicated server process (the edited scene, saved to .cache/net) + the editor as a client
//                   + players - 1 more client processes
#include "Net/NetWorld.h"
#include <string>
#include <vector>

namespace bdg {
class EditorModel;
class PlayMode;
class UIContext;
struct Widget;

struct PlayOptions {
  NetMode mode = NetMode::Standalone;
  int players = 1;          // 1..4
  int conditions = 0;       // 0 off, 1 LAN, 2 internet, 3 bad (presets of latency / jitter / loss)
  uint16_t port = 7777;     // 0 = any free port
  float latencyMs = 0, jitterMs = 0, lossPercent = 0;
  uint32_t timeoutMs = 5000;          // no answer this long = gone (the editor and every launched process)
  std::vector<std::string> extraArgs; // for every launched process (tests: --quit-after ...)
  std::string projectDir;             // the processes' --project ("" = the working folder)
};
const char* ConditionsName(int preset); // "Off", "LAN", "Internet", "Bad"
void ApplyConditions(PlayOptions& o, int preset);

class PlaySession {
public:
  static PlaySession& Instance();
  PlayOptions options;

  void OpenMenu(UIContext& ui, const Widget& button); // the drop-down next to Play
  std::string Summary() const;                         // "Listen Server · 3P · Internet"
  // Around Play: settings for the editor's game (before PlayMode::Start), then the other players (after it).
  void BeforePlay(PlayMode& play, const EditorModel& model);
  void AfterPlay(PlayMode& play);
  void Stop(); // closes every process it opened
  // One more client process (tests: a late joiner). Returns its index or -1.
  int LaunchClient(const std::string& title, const std::vector<std::string>& extraArgs = {});
  struct Process { std::string title, log; void* handle = nullptr; };
  const std::vector<Process>& Processes() const { return procs_; }
  bool Running(size_t index) const;
  std::string ServerAddress() const { return "127.0.0.1"; }
  uint16_t ServerPort() const { return port_; }

private:
  bool Launch(const std::string& title, std::vector<std::string> args);
  std::vector<Process> procs_;
  uint16_t port_ = 0;
  int clients_ = 0;
};

} // namespace bdg
