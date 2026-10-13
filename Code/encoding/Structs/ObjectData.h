#pragma once

#include <Structs/GameId.h>
#include <Structs/LockData.h>
#include <Structs/Inventory.h>
#include <Structs/GridCellCoords.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/Rotator2_NetQuantize.h>

struct ObjectData
{
    ObjectData() = default;
    ~ObjectData() = default;

    bool operator==(const ObjectData& acRhs) const noexcept;
    bool operator!=(const ObjectData& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    uint32_t ServerId{};
    GameId Id{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CurrentCoords{};
    Vector3_NetQuantize Position{};
    Rotator2_NetQuantize Rotation{};
    LockData CurrentLockData{};
    Inventory CurrentInventory{};
    uint32_t LocalReferenceId{};
    bool IsDroppedItem{};
    bool IsSenderFirst{};
};
