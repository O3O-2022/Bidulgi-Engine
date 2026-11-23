// Bidulgi Editor entry point. Uses the engine only through the C API (bdg_api.h).
// Usage: BidulgiEditor [--frames N] [--size WxH] [--dump-tokens out.json]
//   --frames N        quit after N frames (smoke test)
//   --size WxH        window size in UI units (design parity test uses the browser's 800x600)
//   --dump-tokens P   write the design token values the engine uses
//   --replay P        execute a command log (JSONL) after start-up
#include <bdg/bdg_api.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  bdg_engine_desc desc{};
  desc.title = "Bidulgi Engine - Editor";
  desc.width = 1280;
  desc.height = 720;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) desc.max_frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--dump-tokens") == 0 && i + 1 < argc) desc.dump_tokens_path = argv[++i];
    else if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) desc.replay_path = argv[++i];
    else if (std::strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
      char* end = nullptr;
      long w = std::strtol(argv[++i], &end, 10), h = (end && *end == 'x') ? std::strtol(end + 1, nullptr, 10) : 0;
      if (w > 0 && h > 0) { desc.width = (int32_t)w; desc.height = (int32_t)h; }
    }
  }

  bdg_engine* engine = nullptr;
  bdg_result r = bdg_engine_create(&desc, &engine);
  if (r != BDG_OK) { std::fprintf(stderr, "[error] engine create: %s\n", bdg_result_string(r)); return 1; }
  r = bdg_engine_run(engine);
  bdg_engine_destroy(engine);
  return r == BDG_OK ? 0 : 1;
}
