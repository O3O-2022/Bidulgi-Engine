#pragma once
// Project settings (Docs/features/project-settings.md, Unreal's Project Settings): what the game is called and how
// it is packaged and run. Reflected structs, so the settings panel, the JSON file and `bdg` read them like any
// other engine data. Saved to ProjectSettings/Project.json, only the values that differ from these defaults
// (the file stays short and a new engine default reaches every project that never changed it).
#include "Core/Math.h"
#include "Core/Reflection.h"
#include <string>

namespace bdg {

BENUM()
enum class BuildConfig { Debug, Development, Shipping };
BENUM()
enum class WindowMode { Windowed, Borderless, Fullscreen };
BENUM()
enum class ScriptLanguage { CSharp, Lua, Graph, Cpp, Python };
BENUM()
enum class StartQuality { Auto, Potato, Low, Medium, High, Ultra };

BSTRUCT()
struct ProjectDescription {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Header = "게임", Tooltip = "Shown in the window title, the installer and the store page") std::string name = "My Game";
  BPROPERTY(Edit, Tooltip = "Semantic version (major.minor.patch); every build writes it into the game") std::string version = "0.1.0";
  BPROPERTY(Edit, Tooltip = "A number that only goes up, for stores that need one (Android versionCode)") int32_t buildNumber = 1;
  BPROPERTY(Edit, Tooltip = "One or two lines about the game") std::string description;
  BPROPERTY(Edit, Header = "제작자", Tooltip = "Studio or company name (also the save folder: %APPDATA%/<company>/<name>)") std::string company = "My Studio";
  BPROPERTY(Edit, Tooltip = "Person or team credited as the maker") std::string author;
  BPROPERTY(Edit, Tooltip = "Copyright line shown in the build's file properties") std::string copyright = "Copyright (c) My Studio";
  BPROPERTY(Edit, Tooltip = "Web site or store link") std::string homepage;
  BPROPERTY(Edit, Tooltip = "Where players report problems") std::string supportEmail;
  BPROPERTY(Edit, Header = "식별", Tooltip = "Reverse domain id, unique per game (com.studio.game): Android package, iOS bundle id, save keys") std::string identifier = "com.mystudio.mygame";
  BPROPERTY(Edit, Tooltip = "Square image, 1024 px or more (project-relative path); every platform icon is made from it") std::string icon;
  BPROPERTY(Edit, Tooltip = "Image shown while the game starts (empty: none)") std::string splash;
};

BSTRUCT()
struct ProjectMaps {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "The scene the built game starts with") std::string gameStartScene = "Scenes/Main.scene";
  BPROPERTY(Edit, Tooltip = "The scene the editor opens (empty: the last one opened)") std::string editorStartScene;
  BPROPERTY(Edit, Tooltip = "Scenes packed into the build, separated by ';' (empty: every scene the start scene can reach)") std::string scenesInBuild;
};

BSTRUCT()
struct ProjectPackaging {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Debug: full checks / Development: fast with the console and logs / Shipping: what players get") BuildConfig config = BuildConfig::Development;
  BPROPERTY(Edit, Tooltip = "Folder the builds go to (project-relative)") std::string outputFolder = "Builds";
  BPROPERTY(Edit, Tooltip = "Executable name without extension (empty: the game name)") std::string executableName;
  BPROPERTY(Edit, Header = "내용", Tooltip = "Compress the packed assets (smaller download, slightly slower first load)") bool compressAssets = true;
  BPROPERTY(Edit, Tooltip = "Leave out assets nothing in the build refers to") bool stripUnusedAssets = true;
  BPROPERTY(Edit, Tooltip = "Never pack editor-only assets and modules") bool excludeEditorContent = true;
  BPROPERTY(Edit, Tooltip = "Keep symbol files next to the build so crash reports show function names") bool debugSymbols = true;
  BPROPERTY(Edit, Tooltip = "Shipping builds keep the developer console (off: no console for players)") bool consoleInShipping = false;
  BPROPERTY(Edit, Header = "결과물", Tooltip = "Also make a .zip of the build folder") bool zipBuild = false;
  BPROPERTY(Edit, Tooltip = "Explain a failing build step by step instead of one error (7-4b)") bool explainFailures = true;
};

BSTRUCT()
struct ProjectPlatforms {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Windows 10/11, 64-bit") bool windows = true;
  BPROPERTY(Edit) bool linux = false;
  BPROPERTY(Edit) bool macOS = false;
  BPROPERTY(Edit, Tooltip = "GLES 3.0 / Vulkan (1.0 plan)") bool android = false;
  BPROPERTY(Edit) bool iOS = false;
  BPROPERTY(Edit, Tooltip = "WebGL2 in the browser (1.0 plan)") bool web = false;
};

BSTRUCT()
struct ProjectDisplay {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "How the game window opens the first time (players can change it in the settings menu)") WindowMode windowMode = WindowMode::Windowed;
  BPROPERTY(Edit, Range = "320,7680") int32_t width = 1920;
  BPROPERTY(Edit, Range = "240,4320") int32_t height = 1080;
  BPROPERTY(Edit, Tooltip = "Players may resize the window") bool resizable = true;
  BPROPERTY(Edit, Tooltip = "Wait for the monitor's refresh (no tearing)") bool vsync = true;
  BPROPERTY(Edit, Range = "0,360", Tooltip = "Frames per second at most (0 = no limit)") int32_t frameLimit = 0;
  BPROPERTY(Edit, Tooltip = "Keep running while the window is in the background") bool runInBackground = false;
  BPROPERTY(Edit, Tooltip = "Show the mouse cursor in the game") bool showCursor = true;
  BPROPERTY(Edit, Tooltip = "Quality tier the game starts with (Auto: measured on the player's machine)") StartQuality startQuality = StartQuality::Auto;
};

BSTRUCT()
struct ProjectPhysics {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Metres per second squared (Y is up)") Vec3 gravity{0, -9.81f, 0};
  BPROPERTY(Edit) Vec2 gravity2D{0, -9.81f};
  BPROPERTY(Edit, Range = "0.001,0.1", Tooltip = "Seconds per physics / FixedUpdate step (1/60 = 0.0167)") float fixedTimestep = 1.f / 60.f;
  BPROPERTY(Edit, Range = "1,16", Tooltip = "Fixed steps a slow frame may run at most (the rest of the time is dropped)") int32_t maxFixedSteps = 4;
};

BSTRUCT()
struct ProjectLocalization {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Language code the game starts in when the system language is not supported (ko, en, ja ...)") std::string defaultLanguage = "ko";
  BPROPERTY(Edit, Tooltip = "Language codes the game ships with, separated by ','") std::string languages = "ko,en";
  BPROPERTY(Edit, Tooltip = "Follow the player's system language when it is one of the languages") bool useSystemLanguage = true;
};

BSTRUCT()
struct ProjectAudio {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "22050,96000") int32_t sampleRate = 48000;
  BPROPERTY(Edit, Range = "0,1") float masterVolume = 1.f;
  BPROPERTY(Edit, Tooltip = "Keep playing sound while the window is in the background") bool playInBackground = false;
};

BSTRUCT()
struct ProjectScripting {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Language new scripts are made in") ScriptLanguage defaultLanguage = ScriptLanguage::CSharp;
  BPROPERTY(Edit, Tooltip = "Reload changed C# scripts while the editor runs (4-4)") bool hotReload = true;
  BPROPERTY(Edit, Tooltip = "Treat script warnings as errors in builds") bool warningsAsErrors = false;
};

BSTRUCT()
struct ProjectSavesPrivacy {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "Where player saves go ({company} and {name} are filled in)") std::string saveFolder = "%APPDATA%/{company}/{name}/Saves";
  BPROPERTY(Edit, Tooltip = "Cloud save through the store when it offers one") bool cloudSaves = false;
  BPROPERTY(Edit, Tooltip = "Ask players before sending a crash report (never sent silently)") bool crashReports = true;
  BPROPERTY(Edit, Tooltip = "Anonymous usage statistics (off by default; players are asked first)") bool analytics = false;
  BPROPERTY(Edit, Range = "0,21", Tooltip = "Minimum player age the store page states (0 = not set)") int32_t ageRating = 0;
};

BSTRUCT()
struct ProjectSettings {
  GENERATED_BODY()
public:
  BPROPERTY(Edit) ProjectDescription description;
  BPROPERTY(Edit) ProjectMaps maps;
  BPROPERTY(Edit) ProjectPackaging packaging;
  BPROPERTY(Edit) ProjectPlatforms platforms;
  BPROPERTY(Edit) ProjectDisplay display;
  BPROPERTY(Edit) ProjectPhysics physics;
  BPROPERTY(Edit) ProjectLocalization localization;
  BPROPERTY(Edit) ProjectAudio audio;
  BPROPERTY(Edit) ProjectScripting scripting;
  BPROPERTY(Edit) ProjectSavesPrivacy saves;
};

// ProjectSettings/Project.json <-> settings. Unknown keys are kept out (warned), missing ones keep the default.
bool LoadProjectSettings(const std::string& path, ProjectSettings& out, std::string* error = nullptr);
bool SaveProjectSettings(const std::string& path, const ProjectSettings& s, std::string* error = nullptr);
std::string ProjectSettingsDiffJson(const ProjectSettings& s); // only values that differ from the defaults
// Problems a build would hit (empty name, bad version, bad identifier, no platform...). Shown in the panel.
std::vector<std::string> ValidateProjectSettings(const ProjectSettings& s);

} // namespace bdg
