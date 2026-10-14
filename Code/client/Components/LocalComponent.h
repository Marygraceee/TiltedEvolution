#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/ActionEvent.h>

struct LocalComponent
{
    LocalComponent(uint32_t aId, uint32_t aOwnershipEpoch, uint32_t aCachedRefId) noexcept
        : Id(aId)
        , OwnershipEpoch(aOwnershipEpoch)
        , CachedRefId(aCachedRefId)
    {
    }

    uint32_t Id;
    uint32_t OwnershipEpoch;
    uint32_t CachedRefId;
    ActionEvent CurrentAction;
    bool IsDead = false;
    bool IsWeaponDrawn = false;
};
