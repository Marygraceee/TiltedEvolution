#pragma once

#include "Message.h"
#include <Structs/GridCellCoords.h>
#include <Structs/Inventory.h>
#include <Structs/ObjectData.h>

struct RequestInventoryChanges final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestInventoryChanges;

    RequestInventoryChanges()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestInventoryChanges() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestInventoryChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OwnershipEpoch == acRhs.OwnershipEpoch && Item == acRhs.Item && DropId == acRhs.DropId &&
               ReferenceFormId == acRhs.ReferenceFormId && SenderCellId == acRhs.SenderCellId && SenderWorldSpaceId == acRhs.SenderWorldSpaceId &&
               SenderCurrentCoords == acRhs.SenderCurrentCoords && HasSenderCell == acRhs.HasSenderCell && DroppedObject == acRhs.DroppedObject &&
               HasDroppedObject == acRhs.HasDroppedObject && Drop == acRhs.Drop && UpdateClients == acRhs.UpdateClients;
    }

    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    Inventory::Entry Item{};
    uint32_t DropId{};
    uint32_t ReferenceFormId{};
    GameId SenderCellId{};
    GameId SenderWorldSpaceId{};
    GridCellCoords SenderCurrentCoords{};
    bool HasSenderCell{};
    ObjectData DroppedObject{};
    bool HasDroppedObject{};
    bool Drop = false;
    bool UpdateClients = true;
};
