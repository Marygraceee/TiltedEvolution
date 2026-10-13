#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <cstdint>
#include <utility>
#include <Structs/ObjectData.h>

struct DroppedItemComponent
{
    DroppedItemComponent(ObjectData aData, uint32_t aOriginPlayerId)
        : Data(std::move(aData))
        , OriginPlayerId(aOriginPlayerId)
    {
    }

    ObjectData Data{};
    uint32_t OriginPlayerId{};
};
