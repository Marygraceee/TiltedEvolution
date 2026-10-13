#pragma once

#include "Message.h"

#include <Structs/ObjectData.h>

using TiltedPhoques::Vector;

struct AssignObjectsResponse final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kAssignObjectsResponse;

    AssignObjectsResponse()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const AssignObjectsResponse& achRhs) const noexcept
    {
        return GetOpcode() == achRhs.GetOpcode() && CellId == achRhs.CellId && WorldSpaceId == achRhs.WorldSpaceId && CurrentCoords == achRhs.CurrentCoords &&
               Objects == achRhs.Objects;
    }

    GameId CellId{};
    GameId WorldSpaceId{};
    GridCellCoords CurrentCoords{};
    Vector<ObjectData> Objects{};
};
