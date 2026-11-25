#pragma once
// Game builds (queues 7-4, 7-4b): `bdg build` turns a project folder into a folder players can run.
//
//   <out>/<Game>.exe            the player (BidulgiPlayer.exe renamed, the project icon in it)
//   <out>/Content/game.bpak     ProjectSettings, the scenes in the build, the assets they use (+ .meta and cooked
//                               textures), Data/ tables
//   <out>/Managed/              the C# runtime glue (only when the game has C# scripts)
//   <out>/Scripts/*.dll         the game's script assemblies (.pdb too unless Shipping without debug symbols)
//   <out>/THIRD-PARTY-NOTICES.txt, <out>/build-report.txt
//   <outputFolder>/<Game>-<version>-<config>.zip  (packaging.zipBuild)
//
// Steps: check -> collect -> cook -> scripts -> pack -> copy -> report. The check (also `bdg build --check`) looks
// at everything first and lists every problem, one line each: step, what, why, how to fix. A build with errors
// stops before writing anything.
#include <string>
#include <vector>

namespace bdg {

struct BuildOptions {
  std::string project;                 // project folder (ProjectSettings/, Scenes/, Assets/)
  std::string output;                  // "" = <project>/<packaging.outputFolder>/Windows-<config>
  std::string playerExe;               // "" = BidulgiPlayer.exe next to the running program
  std::string managedDir;              // "" = <folder of the running program>/Managed
  std::vector<std::string> scriptDlls; // more script assemblies (besides <project>/.cache/Scripts/*.dll)
  std::string config;                  // "" = packaging.config; Debug | Development | Shipping
  int zip = -1;                        // -1 = packaging.zipBuild, 0 no, 1 yes
};

struct BuildProblem {
  bool error = true;      // false: a warning (the build goes on)
  std::string step;       // 설정, 장면, 에셋, 스크립트, 아이콘, 출력, ...
  std::string what;       // the file or setting ("Scenes/Main.scene:4 Player · MeshRenderer.mesh")
  std::string cause;      // why it is a problem
  std::string fix;        // how to fix it
  std::string Line() const; // "[오류] 장면 · Scenes/Main.scene — 파일이 없음 → maps.gameStartScene을 고치세요"
};

struct BuildResult {
  bool ok = false;
  std::vector<BuildProblem> problems; // in the order found
  std::string outDir, exePath, pakPath, reportPath, zipPath;
  size_t files = 0;                   // packed files
  unsigned long long pakBytes = 0, rawBytes = 0;
  int Errors() const;
  int Warnings() const;
};

// Pre-check only (7-4b): every problem, nothing written.
BuildResult CheckBuild(const BuildOptions& options);
// Check, then build when there is no error.
BuildResult BuildGame(const BuildOptions& options);

// Writes a .zip (stored, CRC32) of every file under `dir`, paths relative to it. (Used by the build; tests.)
bool WriteZipOfFolder(const std::string& dir, const std::string& zipPath, std::string* error = nullptr);
// Puts a PNG (any size; scaled to 256 px) into a Windows .exe as its icon (icon group 1). Windows only.
bool SetExeIcon(const std::string& exePath, const std::string& pngPath, std::string* error = nullptr);

} // namespace bdg
