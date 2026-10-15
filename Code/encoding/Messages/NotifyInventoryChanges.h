#pragma once

#include "Message.h"

#include <Structs/Inventory.h>
#include <Structs/ObjectData.h>

struct NotifyInventoryChanges final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyInventoryChanges;

    NotifyInventoryChanges()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyInventoryChanges& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && OwnershipEpoch == acRhs.OwnershipEpoch && Item == acRhs.Item && DropId == acRhs.DropId &&
               DroppedObject == acRhs.DroppedObject && HasDroppedObject == acRhs.HasDroppedObject && Drop == acRhs.Drop && ReferenceFormId == acRhs.ReferenceFormId &&
               DropRejected == acRhs.DropRejected;
    }

    uint32_t ServerId{};
    uint32_t OwnershipEpoch{};
    Inventory::Entry Item{};
    uint32_t DropId{};
    ObjectData DroppedObject{};
    bool HasDroppedObject{};
    bool Drop = false;
    uint32_t ReferenceFormId{};
    bool DropRejected{};
};
