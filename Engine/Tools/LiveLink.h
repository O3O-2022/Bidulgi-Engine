#pragma once
// Live editor link for `bdg mcp` (AI-4, Docs/features/mcp.md): the open editor listens on 127.0.0.1 (a free port) and
// writes <project>/.cache/editor.json {"port","pid","token"}; `bdg mcp` sends a `bdg` command line there, the editor
// runs it on its own scene through its own command bus (one undo step, shown at once) and answers the --json reply.
//
// Wire: one connection per request, one JSON line each way. Request {"token":"...","cli":["set","Player","hp=5"]}.
// Requests are queued by a listener thread and run on the editor's main thread (Server::Serve, once a frame).
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bdg::live {

class Server {
public:
  ~Server() { Stop(); }
  // Listens and writes <root>/.cache/editor.json. false (and why) when the socket could not open.
  bool Start(const std::filesystem::path& root, std::string* error = nullptr);
  void Stop(); // closes the socket, removes editor.json (only if it is still ours)
  bool Running() const { return running_; }
  int Port() const { return port_; }
  // Main thread: runs the queued requests; handler gets the command line and returns the JSON answer.
  int Serve(const std::function<std::string(const std::vector<std::string>& cli)>& handler);
  std::function<void()> wake; // listener thread: a request is waiting (the editor wakes its event loop)

private:
  struct Request {
    std::vector<std::string> cli;
    std::string answer;
    bool done = false;
  };
  void Listen();
  std::filesystem::path file_;
  std::string token_;
  std::atomic<bool> running_{false};
  int port_ = 0;
  uintptr_t socket_ = ~uintptr_t(0);
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable answered_;
  std::deque<Request*> queue_;
};

// `bdg mcp` side: sends {"cli":[...]} (requestJson) to the editor named in <root>/.cache/editor.json; returns its JSON
// answer, or "" when there is no editor file or it does not answer (then the caller edits the files).
std::string Forward(const std::filesystem::path& root, const std::string& requestJson, int timeoutMs = 120000);

} // namespace bdg::live
