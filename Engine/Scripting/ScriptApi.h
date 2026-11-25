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
// Answer: {"ok":true,"value":...} or {"ok":false,"error":"..."}.
#include "Core/Command.h"
#include <string>
#include <string_view>

namespace bdg {
std::string HandleScriptRequest(Scene& scene, CommandBus& bus, std::string_view requestJson);
}
