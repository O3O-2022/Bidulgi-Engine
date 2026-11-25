#pragma once
// Multiplayer components (round 7, M-3, Docs/features/multiplayer.md), Unreal-style and server-authoritative:
//  - NetworkObject: the object exists for everyone. The server gives it a net id; objects spawned on the server
//    appear on every client and go away with it. Fields marked Replicated (BPROPERTY(Replicated), C# [Replicated])
//    of its components go from the server to the clients when they change.
//  - NetworkTransform: its Transform goes to the clients many times a second; they draw it a little in the past,
//    interpolated (smooth even with lost packets). The owner's client moves it itself (prediction) and the server
//    checks each move (speed limit) and corrects the owner when they disagree (reconciliation).
//  - GameMode: the server spawns a player for every player that joins (a .prefab or an object by name) at the
//    PlayerStart objects, owned by that player; a player's objects go when they leave.
//  - PlayerStart: where players appear (or any object whose name starts with "PlayerStart").
#include "Core/Reflection.h"
#include <string>

namespace bdg {

BCLASS(Component)
struct NetworkObject {
  GENERATED_BODY()
public:
  BPROPERTY(ReadOnly, Replicated, Tooltip = "Given by the server") int32_t netId = 0;
  BPROPERTY(ReadOnly, Replicated, Tooltip = "Player that owns it (0 = the server / host)") int32_t owner = 0;
  BPROPERTY(Edit, Range = "0,100000", Tooltip = "Sent only to players within this distance of their own player (m); 0 = everyone") float relevancy = 0.f;
  BPROPERTY(Edit, Tooltip = "Destroyed on the server when its owner leaves") bool destroyWithOwner = true;
};

BCLASS(Component)
struct NetworkTransform {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "1,60", Tooltip = "Updates per second sent to the clients") float sendRate = 30.f;
  BPROPERTY(Edit, Tooltip = "The owner's client moves it itself; the server checks the moves and corrects it") bool ownerPredicted = true;
  BPROPERTY(Edit, Range = "0.1,1000", Tooltip = "Fastest the owner may move it (m/s); faster moves are cut by the server") float maxSpeed = 20.f;
};

BCLASS(Component)
struct GameMode {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Tooltip = "What every player gets: a .prefab or the name of an object to copy") std::string playerPrefab;
  BPROPERTY(Edit, Tooltip = "The host of a listen server gets a player too") bool hostHasPlayer = true;
  BPROPERTY(Edit, Range = "1,64", Tooltip = "Players at most") int32_t maxPlayers = 8;
  BPROPERTY(ReadOnly, Replicated, Tooltip = "Players connected now") int32_t players = 0;
};

BCLASS(Component)
struct PlayerStart {
  GENERATED_BODY()
public:
  BPROPERTY(Edit, Range = "0,64", Tooltip = "Order in which starts are used (lowest first)") int32_t order = 0;
};

} // namespace bdg
