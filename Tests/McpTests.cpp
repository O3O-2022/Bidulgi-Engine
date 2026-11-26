// `bdg mcp` (AI-4): JSON-RPC framing, the MCP handshake and tool dispatch, with a fake `bdg` behind the tools.
#include "bdg_test.h"
#include "Tools/Mcp.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

struct Fake {
  std::vector<std::vector<std::string>> calls;
  bdg::McpServer server{"C:/proj", "C:/"};
  Fake() {
    server.cli = [this](const std::vector<std::string>& args, std::string& out, std::string&) {
      calls.push_back(args);
      if (args[0] == "set" && args[1] == "missing") {
        out = R"({"ok":false,"exit":1,"command":"set","output":[],"error":{"code":"not_found","message":"no object missing"}})";
        return 1;
      }
      out = R"({"ok":true,"exit":0,"command":")" + args[0] + R"(","output":["line 1"]})" "\n";
      return 0;
    };
  }
  std::string Call(const std::string& tool, const std::string& arguments) {
    return server.Handle(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":")" + tool + R"(","arguments":)" + arguments + "}}");
  }
};
} // namespace

TEST_CASE("mcp: JSON-RPC framing, initialize, notifications, errors, batches (AI-4)") {
  Fake f;
  const std::string init = f.server.Handle(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})");
  CHECK(Has(init, R"("id":1)"));
  CHECK(Has(init, R"("protocolVersion":"2025-03-26")")); // the client's supported version is kept
  CHECK(Has(init, R"("tools":)") && Has(init, R"("resources":)") && Has(init, R"("name":"bidulgi")"));
  bdg::McpServer other("C:/p", "C:/");
  CHECK(Has(other.Handle(R"({"jsonrpc":"2.0","id":"a","method":"initialize","params":{"protocolVersion":"2099-01-01"}})"), R"("protocolVersion":"2025-06-18")"));
  CHECK_EQ(f.server.Handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"), std::string()); // no answer
  CHECK(f.server.initialized);
  CHECK_EQ(f.server.Handle(R"({"jsonrpc":"2.0","id":2,"method":"ping"})"), std::string(R"({"jsonrpc":"2.0","id":2,"result":{}})"));
  CHECK(Has(f.server.Handle("{not json"), "-32700"));
  CHECK(Has(f.server.Handle(R"({"jsonrpc":"2.0","id":3,"method":"nope"})"), "-32601"));
  CHECK(Has(f.server.Handle(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"nope"}})"), "-32602"));
  CHECK(Has(f.server.Handle(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"set","arguments":{"values":{}}}})"), "-32602")); // target missing
  const std::string batch = f.server.Handle(R"([{"jsonrpc":"2.0","id":10,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/x"},{"jsonrpc":"2.0","id":11,"method":"ping"}])");
  CHECK(batch.front() == '[' && Has(batch, R"("id":10)") && Has(batch, R"("id":11)"));

  // stdio: one message per line, CRLF tolerated, the last line without \n still answered
  std::FILE* in = std::tmpfile();
  std::FILE* out = std::tmpfile();
  const char* text = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\r\n\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}";
  std::fputs(text, in);
  std::rewind(in);
  CHECK_EQ(f.server.RunStdio(in, out), 0);
  std::rewind(out);
  std::string got;
  for (int ch; (ch = std::fgetc(out)) != EOF;) got += (char)ch;
  CHECK_EQ(got, std::string("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}\n"));
  std::fclose(in);
  std::fclose(out);

  CHECK_EQ(bdg::Base64(reinterpret_cast<const unsigned char*>("Man"), 3), std::string("TWFu"));
  CHECK_EQ(bdg::Base64(reinterpret_cast<const unsigned char*>("Ma"), 2), std::string("TWE="));
  CHECK_EQ(bdg::Base64(reinterpret_cast<const unsigned char*>("M"), 1), std::string("TQ=="));
}

TEST_CASE("mcp: tools/list and tools/call dispatch to bdg commands (AI-4)") {
  Fake f;
  const std::string list = f.server.Handle(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
  for (const char* t : {"scene_view", "scene_outline", "query", "schema", "set", "add", "del", "addcomp", "validate", "shot", "run_check", "build", "read_log",
                        "list_docs", "read_doc"})
  {
    const std::string want = std::string("\"name\":\"") + t + "\"";
    CHECK(Has(list, want));
  }
  CHECK(Has(list, R"("dry_run":{"type":"boolean")") && Has(list, R"("readOnlyHint":true)"));
  CHECK(!Has(list, "SCENE") && !Has(list, "DRY") && !Has(list, "{,"));

  const std::string view = f.Call("scene_view", R"({"scene":"Level2"})");
  CHECK(Has(view, R"("isError":false)") && Has(view, R"("structuredContent":{"ok":true)"));
  CHECK(f.calls.back() == std::vector<std::string>({"view", "--scene", "Level2", "--project", "C:/proj", "--json"}));

  f.Call("set", R"({"target":"Player","values":{"Health.hp":20,"pos":[6,0,4.5],"name":"Boss","Light.on":true},"dry_run":true})");
  CHECK(f.calls.back() == std::vector<std::string>({"set", "Player", "Health.hp=20", "pos=6,0,4.5", "name=Boss", "Light.on=true", "--dry-run", "--project", "C:/proj", "--json"}));
  f.Call("add", R"({"id":"Crate","pos":[1,2,3],"components":["MeshRenderer","RigidBody"]})");
  CHECK(f.calls.back() == std::vector<std::string>({"add", "Crate", "--pos", "1,2,3", "--comp", "MeshRenderer", "--comp", "RigidBody", "--project", "C:/proj", "--json"}));
  f.Call("del", R"({"ids":["A","B*"]})");
  CHECK(f.calls.back() == std::vector<std::string>({"del", "A", "B*", "--project", "C:/proj", "--json"}));
  f.Call("addcomp", R"({"id":"A","type":"Light","dry_run":true})");
  CHECK(f.calls.back() == std::vector<std::string>({"addcomp", "A", "Light", "--dry-run", "--project", "C:/proj", "--json"}));
  f.Call("query", R"({"pattern":"Enemy*","fields":"pos,Health.hp"})");
  CHECK(f.calls.back() == std::vector<std::string>({"query", "Enemy*", "-f", "pos,Health.hp", "--project", "C:/proj", "--json"}));
  f.Call("run_check", R"({"test":"BDG_FPS_TEST","frames":300,"env":{"BDG_X":"2"}})");
  CHECK(f.calls.back() == std::vector<std::string>({"check", "BDG_FPS_TEST", "--frames", "300", "--env", "BDG_X=2", "--project", "C:/proj", "--json"}));
  f.Call("validate", R"({"dry_run":true})"); // dry_run only for edits
  CHECK(f.calls.back() == std::vector<std::string>({"validate", "--project", "C:/proj", "--json"}));

  const std::string bad = f.Call("set", R"({"target":"missing","values":{"x":1}})");
  CHECK(Has(bad, R"("isError":true)") && Has(bad, "not_found"));

  // live editor: edits go there (as a command line) while it answers; reads stay on files
  std::string sent;
  f.server.live = [&](const std::string& req) {
    sent = req;
    return std::string(R"({"ok":true,"exit":0,"command":"set","output":["live"]})");
  };
  const size_t before = f.calls.size();
  const std::string live = f.Call("set", R"({"target":"Player","values":{"Health.hp":5}})");
  CHECK_EQ(sent, std::string(R"({"cli":["set","Player","Health.hp=5"]})"));
  CHECK(Has(live, R"("output":["live"])") && Has(live, R"("isError":false)") && f.calls.size() == before);
  f.Call("scene_view", "{}");
  CHECK(f.calls.size() == before + 1);
  f.server.live = [](const std::string&) { return std::string(); }; // editor gone: files again
  f.Call("set", R"({"target":"Player","values":{"Health.hp":5}})");
  CHECK(f.calls.size() == before + 2);

  const std::string res = f.server.Handle(R"({"jsonrpc":"2.0","id":9,"method":"resources/list"})");
  CHECK(Has(res, "bidulgi://docs/llms.txt") && Has(res, "bidulgi://schema/components"));
  CHECK(Has(f.server.Handle(R"({"jsonrpc":"2.0","id":9,"method":"resources/read","params":{"uri":"bidulgi://schema/components"}})"), R"("contents":[{"uri":"bidulgi://schema/components")"));
  CHECK(f.calls.back()[0] == "schema");
  CHECK(Has(f.server.Handle(R"({"jsonrpc":"2.0","id":9,"method":"resources/read","params":{"uri":"bidulgi://nope"}})"), R"("error")"));
}
