#pragma once
// Game templates (Docs/features/projects-and-templates.md §4): `bdg new <dir> --template fps` makes a playable
// project from Templates/<name>/ (shipped next to bdg.exe). Text files get the game's name and the engine's C#
// folder filled in ({{GAME_NAME}}, {{GAME_ASSEMBLY}}, {{BDG_MANAGED}}), the sounds listed in template.json are
// generated (CC0: synthesized, no recordings), and the C# scripts are built once with `dotnet build` when the
// .NET SDK is there.
#include <string>
#include <vector>

namespace bdg {

struct GameTemplateInfo {
  std::string name, title, description;
};

std::string GameTemplatesDir();                  // <program dir>/Templates (or the source tree's, for developers)
std::vector<GameTemplateInfo> GameTemplates();   // the templates found there
bool IsGameTemplate(const std::string& name);

struct TemplateResult {
  std::vector<std::string> files; // written, project-relative
  bool scriptsBuilt = false;
  std::string scriptLog;          // dotnet build output (last lines) when it failed or was skipped
};
// dir must not hold a project yet. gameName "" = the folder name.
bool CreateFromTemplate(const std::string& name, const std::string& dir, const std::string& gameName, TemplateResult& out,
                        std::string* error = nullptr, bool buildScripts = true);

// A short generated sound (mono 16-bit, 44.1 kHz): "shoot", "hit", "jump", "coin", "goal". Deterministic.
bool WriteGeneratedSound(const std::string& kind, const std::string& path, std::string* error = nullptr);

} // namespace bdg
