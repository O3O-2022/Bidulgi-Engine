#pragma once
// Engine side of the script request channel (queue 4-1). A script host (C# now, Lua in M5) sends one JSON request
// per engine call and gets one JSON answer; set / call / rename go through the command bus with source "script",
// so script edits undo, log and replay like every other edit.
//
//   {"op":"log","level":"info|warn|error","text":"..."}                     -> null
//   {"op":"find","name":"Player"}            (id or name)                    -> "g6" or null
//   {"op":"name","target":"g6"}  /  {"op":"rename","target":"g6","name":"P"} -> "Player" / null
//   {"op":"has","target":"g6","type":"Health"}                               -> true / false
//   {"op":"get","target":"g6","path":"Health.current"}                       -> 90
//   {"op":"set","target":"g6","path":"Health.current","value":90}            -> null (one undo step)
//   {"op":"call","target":"g6","type":"Health","function":"Damage","args":[10]} -> 80 (Pure: no undo step)
// Easy API (4-4c):
//   {"op":"children","target":"g6","deep":true}                             -> ["g7","g9"] (hierarchy order)
//   {"op":"findall","name":"Coin"}           ("" = every object)             -> [{"id":"g7","name":"Coin","pos":[x,y,z]}]
//   {"op":"spawn","source":"g6","name":"Copy","parent":"","position":[..],"rotation":[..]} -> "g12"
//        (source: a copy of that object and its children with new ids; no source: an empty object; one undo step)
//   {"op":"destroy","target":"g6"}                                          -> ["g6","g7"] (the object and its children)
// Audio (6-3), while playing:
//   {"op":"audio.play","clip":"Shot.wav","group":"Effects","volume":1,"pitch":1,"loop":false,"at":[x,y,z]} -> 7 (sound id)
//   {"op":"audio.stop","id":7}  {"op":"audio.playing","id":7} -> true  {"op":"audio.stopall"}
//   {"op":"audio.volume","group":"Music","volume":0.5} -> 0.5 (without "volume": just reads it)
// Saves (6-5b; files in the project's save folder, Scripting/SaveGame.h):
//   {"op":"save.write","slot":"main","data":{...}} -> path   {"op":"save.read","slot":"main"} -> {...} or null
//   {"op":"save.list"} -> ["main"]   {"op":"save.delete","slot":"main"} -> true   {"op":"save.folder"} -> "C:/Users/.../Saves"
// Physics queries (7-1b, 6-5b), while playing:
//   {"op":"raycast","origin":[..],"dir":[..],"max":100,"ignore":["g3"],"triggers":false}
//        -> {"id":"g6","point":[..],"normal":[..],"distance":3.2} or null
//   {"op":"overlap","center":[..],"radius":5,"triggers":false} -> ["g6","g9"]
// Particle effects (6-VFX P-1), while playing:
//   {"op":"vfx.play","effect":"Spark","at":[x,y,z],"rotation":[deg..],"seed":0} -> 3   {"op":"vfx.stop","id":3,"clear":false}
//   {"op":"vfx.playing","id":3} -> true   {"op":"vfx.alive"} -> 120 (all; with "id": that effect's)
// Data tables (4-4d):
//   {"op":"table","name":"Weapons"}  -> {"name":"Weapons","key":"id","rows":[{...}]} (Data/Weapons.table.jsonl or .csv)
// Answer: {"ok":true,"value":...} or {"ok":false,"error":"..."}.
#include "Core/Command.h"
#include <string>
#include <string_view>

namespace bdg {
std::string HandleScriptRequest(Scene& scene, CommandBus& bus, std::string_view requestJson);
}
