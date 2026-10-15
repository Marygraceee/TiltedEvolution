#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Messages/CharacterSpawnRequest.h>

struct WaitingFor3D
{
    WaitingFor3D(const CharacterSpawnRequest& aSpawnRequest, const bool aNeedsCellMove = false)
        : SpawnRequest(aSpawnRequest)
        , NeedsCellMove(aNeedsCellMove)
    {
    }

    CharacterSpawnRequest SpawnRequest{};
    bool NeedsCellMove{};
};
