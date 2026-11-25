#pragma once
// Player saves (queue 6-5b, Docs/features/easy-api.md): one versioned JSON file per slot in the game's save folder.
// Scripts keep the values (C# Save.Set / Get) and hand the whole slot to the engine to write; the engine owns the
// folder, the file format and safe writing (a temporary file renamed over the old one, the previous save kept as
// .bak and read when the newest one is damaged).
//
//   <folder>/<slot>.save.json
//   {"format":"bidulgi-save","version":1,"game":"My Game","gameVersion":"0.1.0","slot":"main",
//    "saved":"2026-11-25T09:30:00Z","data":{"best":12,"coins":40}}
//
// The folder is Project Settings saves.saveFolder ("%APPDATA%/{company}/{name}/Saves" by default) with {company},
// {name} and %VARIABLES% filled in; BDG_SAVE_DIR replaces it (tests, portable builds). The same in the editor and
// in the built player, so a game played in the editor and its build share their saves.
#include <string>
#include <string_view>
#include <vector>

namespace bdg {
struct ProjectSettings;

constexpr int kSaveFormatVersion = 1; // files with a newer version are not read (a newer game wrote them)

// The save folder pattern filled in: {company} and {name} (characters a folder name cannot hold are dropped),
// %VAR% from the environment (an unknown variable stays as it is). Forward slashes.
std::string ExpandSaveFolder(std::string_view pattern, std::string_view company, std::string_view name);
// The folder for the project in the working folder (ProjectSettings/Project.json there, or the defaults); BDG_SAVE_DIR wins.
std::string GameSaveFolder();
std::string GameSaveFolder(const ProjectSettings& settings);

// A slot is a plain name: 1..64 characters, no path separators or characters a file name cannot hold.
bool SaveSlotNameOk(std::string_view slot);
// dataJson must be a JSON object. Creates the folder. error says why it failed.
bool WriteSaveSlot(const std::string& folder, const std::string& slot, std::string_view dataJson, const ProjectSettings& game,
                   std::string* error = nullptr);
// The slot's "data" object as JSON. false when there is no save (error empty) or it cannot be read (error says why:
// damaged with no good .bak, another format, a newer version).
bool ReadSaveSlot(const std::string& folder, const std::string& slot, std::string& dataJson, std::string* error = nullptr);
std::vector<std::string> ListSaveSlots(const std::string& folder); // sorted
bool DeleteSaveSlot(const std::string& folder, const std::string& slot); // the file and its .bak; true when one was there
std::string SaveSlotPath(const std::string& folder, const std::string& slot);
} // namespace bdg
