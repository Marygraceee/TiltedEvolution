#pragma once

#include "Message.h"
#include <Structs/ObjectData.h>
#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>

using TiltedPhoques::Vector;

struct AssignObjectsRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kAssignObjectsRequest;

    AssignObjectsRequest()
        : ClientMessage(Opcode)
    {
    }

    virtual ~AssignObjectsRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AssignObjectsRequest& acRhs) const noexcept
    {
        return Objects == acRhs.Objects && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && CurrentCoords == acRhs.CurrentCoords && GetOpcode() == acRhs.GetOpcode();
    }

    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CurrentCoords{};
    Vector<ObjectData> Objects{};
};
