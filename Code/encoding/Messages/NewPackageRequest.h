#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct NewPackageRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kNewPackageRequest;

    NewPackageRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NewPackageRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ActorId == acRhs.ActorId && OwnershipEpoch == acRhs.OwnershipEpoch && PackageId == acRhs.PackageId;
    }

    uint32_t ActorId;
    uint32_t OwnershipEpoch{};
    GameId PackageId{};
};
