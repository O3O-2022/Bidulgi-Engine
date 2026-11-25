/* bdg_api.h - Bidulgi Engine C API hub.
 * Every language binding (C#, Lua, Python, Graph VM) sees only this header.
 * Rules: C types only, opaque handles, bdg_ prefix. */
#ifndef BDG_API_H
#define BDG_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BDG_VERSION_MAJOR 0
#define BDG_VERSION_MINOR 0
#define BDG_VERSION_PATCH 1

typedef struct bdg_engine bdg_engine; /* opaque handle */

typedef enum bdg_result {
  BDG_OK = 0,
  BDG_ERR_PLATFORM = 1,
  BDG_ERR_GRAPHICS = 2,
  BDG_ERR_INVALID_ARG = 3
} bdg_result;

typedef struct bdg_engine_desc {
  const char* title;      /* UTF-8 */
  int32_t     width;
  int32_t     height;
  int32_t     max_frames; /* 0 = until window closes, >0 = quit after N frames (smoke test) */
  const char* dump_tokens_path; /* optional: write every design token value the engine uses as JSON */
  const char* replay_path;      /* optional: execute a command log (JSONL) after start-up */
} bdg_engine_desc;

bdg_result  bdg_engine_create(const bdg_engine_desc* desc, bdg_engine** out);
bdg_result  bdg_engine_run(bdg_engine* engine); /* main loop */
void        bdg_engine_destroy(bdg_engine* engine);
const char* bdg_result_string(bdg_result r);
const char* bdg_version_string(void);

/* `bdg` command line (queue 2-7): runs one command (argv[0] = program name, the rest UTF-8 arguments) on the
   project in the working folder without opening a window; prints to stdout / stderr. Returns the exit code:
   0 done, 1 problems found or the command failed, 2 bad usage. */
int         bdg_cli_main(int argc, const char* const* argv);

/* Game player (queue 7-4): runs a built game (Content/game.bpak next to the exe, or --project <dir>) without the
   editor UI. Arguments: --frames N, --project <dir>. Returns the process exit code. */
int         bdg_player_main(int argc, const char* const* argv);

#ifdef __cplusplus
}
#endif
#endif
