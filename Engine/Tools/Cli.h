#pragma once
// `bdg` command line (queue 2-7): read and edit a project without the editor, through the same command bus, so an
// AI agent or a script gets the editor's rules (undo-able commands, reflection, byte-stable scene files).
//
//   bdg new <dir> [--template basic3d|empty]      a project folder with Scenes/Main.scene
//   bdg view [scene]                               compact one-line-per-object view
//   bdg outline [scene]                            tree of names with component initials
//   bdg query <pattern> [-f pos,Health.hp]         a table of fields for matching objects (id, name or path glob)
//   bdg schema [Type]                              component signatures (field:type=default)
//   bdg set <target> path=value ... [--dry-run]    pos / rot / scale / name aliases; target may be a glob
//   bdg add <id> [--name N] [--parent P] [--pos x,y,z] [--comp Type]...
//   bdg del <id>...   bdg addcomp <id> <Type>   bdg rmcomp <id> <Type>
//   bdg validate                                   scene, references, orphan .meta, asset rules; exit 1 on errors
//   bdg refs <asset>   bdg unused                  2-5b
//   bdg nodes [filter]                             Graph nodes the reflected components offer (E-8)
//   bdg call <target> <Node.Id> [args...]          run one: Health.Damage 10, Health.Ratio, Health.Set current 5
// Common options: --project <dir> (default: the working folder), --scene <name or path> (default Scenes/Main.scene).
// Writes save the scene and append the commands to <project>/.cache/bdg-commands.jsonl.
#include <filesystem>
#include <string>
#include <vector>

namespace bdg {
// args without the program name. Returns the exit code: 0 done, 1 problems found / command failed, 2 bad usage.
int RunCli(const std::vector<std::string>& args, std::string& out, std::string& err, const std::filesystem::path& cwd);
}
