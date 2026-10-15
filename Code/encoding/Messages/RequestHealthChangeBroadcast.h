#pragma once

#include "Message.h"

struct RequestHealthChangeBroadcast final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestHealthChangeBroadcast;

    RequestHealthChangeBroadcast()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestHealthChangeBroadcast& acRhs) const noexcept
    {
        return Id == acRhs.Id && OwnershipEpoch == acRhs.OwnershipEpoch && DeltaHealth == acRhs.DeltaHealth && HasCurrentHealth == acRhs.HasCurrentHealth &&
               CurrentHealth == acRhs.CurrentHealth && GetOpcode() == acRhs.GetOpcode();
    }

    uint32_t Id;
    uint32_t OwnershipEpoch{};
    float DeltaHealth;
    bool HasCurrentHealth{};
    float CurrentHealth{};
};
