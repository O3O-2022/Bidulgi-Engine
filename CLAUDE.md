# Bidulgi Engine — guide for AI sessions

Start here, in this order:
1. Read `Docs/QUEUE.md` (build steps, test switches, code map, rules, and the ordered task list).
2. If `Docs/QUEUE.md` has a "★ 지금 우선순위" section, follow its order first; otherwise pick the first `[ ]` item, mark it `[~]`, implement it, verify its exit criterion, mark it `[x]` with the commit hash.

If you were started as a parallel session (lane A, B, ...), follow "병렬 세션 규칙" in `Docs/QUEUE.md`: stay in your lane's files, register new checks with `BDG_EDITOR_TEST` (`Engine/Runtime/EditorTests/Registry.h`), commit on your own branch, do not push to main.

Key rules (details in `Docs/QUEUE.md` §0):
- The user speaks Korean: reply and write docs in Korean; code comments and commit messages in English.
- Build: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` then `cmake --build build --config Release --target BidulgiEditor`. Zero warnings (`/W4`).
- Verify with `--frames N` plus `BDG_SCREENSHOT` / `BDG_UI_SELFTEST` / `BDG_DUMP_WIDGETS`; read `Bidulgi.log` in the working folder.
- Only MIT-compatible dependencies; never expose third-party types in `Engine/include/bdg/bdg_api.h`.
- Every edit goes through the command bus once it exists (M1 1-3); every engine type through reflection (M1 1-2).
- Decisions live in `Docs/planning/decisions.md`; add a line when you change order or make a design call.
