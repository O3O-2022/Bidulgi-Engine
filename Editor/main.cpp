// Bidulgi Editor entry point. Uses the engine only through the C API (bdg_api.h).
// Usage: BidulgiEditor [--frames N]   (quit after N frames: smoke test)
#include <bdg/bdg_api.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  bdg_engine_desc desc{};
  desc.title = "Bidulgi Engine - Editor";
  desc.width = 1280;
  desc.height = 720;
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) desc.max_frames = std::atoi(argv[++i]);

  bdg_engine* engine = nullptr;
  bdg_result r = bdg_engine_create(&desc, &engine);
  if (r != BDG_OK) { std::fprintf(stderr, "[error] engine create: %s\n", bdg_result_string(r)); return 1; }
  r = bdg_engine_run(engine);
  bdg_engine_destroy(engine);
  return r == BDG_OK ? 0 : 1;
}
