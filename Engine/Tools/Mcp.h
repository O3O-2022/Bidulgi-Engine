#pragma once
// `bdg mcp` (AI-4, Docs/features/mcp.md): a Model Context Protocol server on stdio, so an AI agent (Claude Code, Claude
// Desktop, any MCP client) can work on a project directly: look at the scene, edit it through the command bus (undoable,
// with a dry run), validate, take a picture, run an automated check, build, read the log and the engine's docs.
//
// Transport: newline-delimited JSON-RPC 2.0 on stdin / stdout (MCP 2025-06-18; 2025-03-26 and 2024-11-05 clients are
// answered in their version). Tools reuse the `bdg` commands with --json, so the AI sees exactly what the CLI does.
// Live mode: when an editor runs on the project (it writes .cache/editor.json), edits go to it instead of the files, so
// the open editor shows them and its undo works.
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bdg {

class McpServer {
public:
  McpServer(std::filesystem::path project, std::filesystem::path cwd);
  // One JSON-RPC message (or a batch array) in; the reply out ("" for notifications and replies to nothing).
  std::string Handle(std::string_view message);
  // Reads messages line by line until the end of the input; replies one line each.
  int RunStdio(std::FILE* in, std::FILE* out);
  // How tools run a `bdg` command (args without "bdg"); defaults to RunCli. Tests replace it.
  std::function<int(const std::vector<std::string>& args, std::string& out, std::string& err)> cli;
  // Live editor forwarding (set by RunMcp when .cache/editor.json names a running editor): a command line for the
  // editor's command bus; returns the editor's JSON answer, or "" when it could not be reached (then files are used).
  std::function<std::string(const std::string& requestJson)> live;
  bool initialized = false;
  std::string protocol = "2025-06-18";

private:
  std::string Call(const std::string& id, const std::string& method, const std::string& paramsJson);
  std::string ToolsList() const;
  std::string ToolCall(const std::string& paramsJson, std::string& error);
  std::string ResourcesList() const;
  std::string ResourceRead(const std::string& paramsJson, std::string& error);
  std::filesystem::path DocsDir() const;
  std::filesystem::path project_, cwd_;
};

// `bdg mcp [--project <dir>]`: the server on this process's stdin / stdout.
int RunMcp(const std::vector<std::string>& args, const std::filesystem::path& cwd);

std::string Base64(const unsigned char* data, size_t size);

} // namespace bdg
