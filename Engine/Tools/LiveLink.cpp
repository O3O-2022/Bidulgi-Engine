#include "Tools/LiveLink.h"
#include <yyjson.h>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

namespace bdg::live {
namespace fs = std::filesystem;
namespace {
#ifdef _WIN32
using Sock = SOCKET;
const Sock kNone = INVALID_SOCKET;
void Close(Sock s) { closesocket(s); }
bool WsaUp() {
  static const bool ok = [] {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }();
  return ok;
}
int Pid() { return (int)GetCurrentProcessId(); }
#endif
using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
Doc Parse(const std::string& s) { return Doc(yyjson_read(s.data(), s.size(), 0), yyjson_doc_free); }
std::string Q(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) o += std::format("\\u{:04x}", (unsigned)c);
    else o += (char)c;
  }
  return o + "\"";
}
// Waits until the socket can be read (or written); false on timeout.
bool Ready(Sock s, bool write, int ms) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  timeval tv{ms / 1000, (ms % 1000) * 1000};
  return select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv) > 0;
}
bool ReadLine(Sock s, std::string& line, int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  char buf[4096];
  while (line.find('\n') == std::string::npos) {
    const int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count();
    if (left <= 0 || !Ready(s, false, left)) return false;
    const int n = recv(s, buf, (int)sizeof(buf), 0);
    if (n <= 0) return false;
    line.append(buf, (size_t)n);
    if (line.size() > (64u << 20)) return false;
  }
  line.resize(line.find('\n'));
  return true;
}
bool SendAll(Sock s, const std::string& data) {
  for (size_t at = 0; at < data.size();) {
    const int n = send(s, data.data() + at, (int)std::min<size_t>(data.size() - at, 1 << 20), 0);
    if (n <= 0) return false;
    at += (size_t)n;
  }
  return true;
}
std::string ErrorJson(const std::string& message) {
  return std::format(R"({{"ok":false,"exit":1,"command":"live","output":[],"error":{{"code":"live","message":{}}}}})", Q(message));
}
} // namespace

bool Server::Start(const fs::path& root, std::string* error) {
  Stop();
  if (!WsaUp()) { if (error) *error = "WSAStartup failed"; return false; }
  Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kNone) { if (error) *error = "socket failed"; return false; }
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // this computer only
  a.sin_port = 0;
  int len = sizeof(a);
  if (bind(s, (sockaddr*)&a, sizeof(a)) != 0 || listen(s, 4) != 0 || getsockname(s, (sockaddr*)&a, &len) != 0) {
    Close(s);
    if (error) *error = "bind/listen on 127.0.0.1 failed";
    return false;
  }
  port_ = ntohs(a.sin_port);
  socket_ = (uintptr_t)s;
  std::random_device rd;
  token_ = std::format("{:08x}{:08x}{:08x}{:08x}", rd(), rd(), rd(), rd());
  std::error_code ec;
  fs::create_directories(root / ".cache", ec);
  file_ = root / ".cache" / "editor.json";
  {
    std::ofstream f(file_, std::ios::binary | std::ios::trunc);
    f << std::format(R"({{"port":{},"pid":{},"token":"{}"}})", port_, Pid(), token_) << '\n';
  }
  running_ = true;
  thread_ = std::thread([this] { Listen(); });
  return true;
}

void Server::Stop() {
  if (!running_ && !thread_.joinable()) return;
  running_ = false;
  if (thread_.joinable()) thread_.join();
  if (socket_ != ~uintptr_t(0)) Close((Sock)socket_);
  socket_ = ~uintptr_t(0);
  { // fail what is still waiting
    std::lock_guard lock(mutex_);
    for (Request* r : queue_) { r->answer = ErrorJson("the editor is closing"); r->done = true; }
    queue_.clear();
  }
  answered_.notify_all();
  std::error_code ec;
  std::ifstream in(file_, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  in.close();
  if (!file_.empty() && ss.str().find(token_) != std::string::npos) fs::remove(file_, ec); // another editor may own it now
}

void Server::Listen() {
  const Sock listener = (Sock)socket_;
  while (running_) {
    if (!Ready(listener, false, 100)) continue;
    const Sock c = accept(listener, nullptr, nullptr);
    if (c == kNone) continue;
    std::string line, answer;
    if (ReadLine(c, line, 5000)) {
      Doc d = Parse(line);
      yyjson_val* root = d ? yyjson_doc_get_root(d.get()) : nullptr;
      const char* token = yyjson_get_str(yyjson_obj_get(root, "token"));
      yyjson_val* cli = yyjson_obj_get(root, "cli");
      if (!token || token_ != token) answer = ErrorJson("wrong token (read .cache/editor.json again)");
      else if (!yyjson_is_arr(cli) || yyjson_arr_size(cli) == 0) answer = ErrorJson("no cli array");
      else {
        Request r;
        size_t i, n;
        yyjson_val* x;
        yyjson_arr_foreach(cli, i, n, x) r.cli.push_back(yyjson_get_str(x) ? yyjson_get_str(x) : "");
        std::unique_lock lock(mutex_);
        queue_.push_back(&r);
        lock.unlock();
        if (wake) wake();
        lock.lock();
        if (!answered_.wait_for(lock, std::chrono::seconds(120), [&] { return r.done || !running_; })) {
          std::erase(queue_, &r);
          r.answer = ErrorJson("the editor did not answer in 120 s");
        } else if (!r.done) {
          std::erase(queue_, &r);
          r.answer = ErrorJson("the editor is closing");
        }
        answer = r.answer;
      }
    }
    if (!answer.empty()) SendAll(c, answer + "\n");
    shutdown(c, SD_BOTH);
    Close(c);
  }
}

int Server::Serve(const std::function<std::string(const std::vector<std::string>&)>& handler) {
  int count = 0;
  for (;;) {
    Request* r = nullptr;
    {
      std::lock_guard lock(mutex_);
      if (queue_.empty()) break;
      r = queue_.front();
      queue_.pop_front();
    }
    std::string answer = handler(r->cli);
    {
      std::lock_guard lock(mutex_);
      r->answer = std::move(answer);
      r->done = true;
    }
    answered_.notify_all();
    ++count;
  }
  return count;
}

std::string Forward(const fs::path& root, const std::string& requestJson, int timeoutMs) {
  std::ifstream in(root / ".cache" / "editor.json", std::ios::binary);
  if (!in) return {};
  std::stringstream ss;
  ss << in.rdbuf();
  Doc info = Parse(ss.str());
  yyjson_val* iv = info ? yyjson_doc_get_root(info.get()) : nullptr;
  const int port = (int)yyjson_get_int(yyjson_obj_get(iv, "port"));
  const char* token = yyjson_get_str(yyjson_obj_get(iv, "token"));
  Doc req = Parse(requestJson);
  yyjson_val* cli = req ? yyjson_obj_get(yyjson_doc_get_root(req.get()), "cli") : nullptr;
  if (port <= 0 || port > 65535 || !token || !cli || !WsaUp()) return {};
  Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == kNone) return {};
  u_long nonBlocking = 1;
  ioctlsocket(s, FIONBIO, &nonBlocking);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons((u_short)port);
  connect(s, (sockaddr*)&a, sizeof(a));
  int soError = 0, soLen = sizeof(soError);
  // a stale editor.json (the editor crashed): nothing listens, the connection is refused at once
  if (!Ready(s, true, 1000) || getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soError, &soLen) != 0 || soError != 0) {
    Close(s);
    return {};
  }
  nonBlocking = 0;
  ioctlsocket(s, FIONBIO, &nonBlocking);
  size_t n = 0;
  char* cliText = yyjson_val_write(cli, 0, &n);
  const std::string line = std::format(R"({{"token":{},"cli":{}}})", Q(token), cliText ? std::string(cliText, n) : "[]") + "\n";
  std::free(cliText);
  std::string answer;
  const bool ok = SendAll(s, line) && ReadLine(s, answer, timeoutMs);
  Close(s);
  return ok ? answer : std::string();
}

} // namespace bdg::live
