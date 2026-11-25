// Game player entry point (queue 7-4): `bdg build` copies this exe into the build folder as <Game>.exe.
// Usage: <Game>.exe [--frames N] [--project <dir>]   (see Engine/Build/Player.cpp)
#include <bdg/bdg_api.h>

int main(int argc, char** argv) { return bdg_player_main(argc, argv); }
