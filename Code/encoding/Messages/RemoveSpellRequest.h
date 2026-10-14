#pragma once
#include "Message.h"
#include <Structs/GameId.h>

struct RemoveSpellRequest final: ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestRemoveSpell;
    RemoveSpellRequest() : ClientMessage(Opcode) {}
    virtual ~RemoveSpellRequest() = default;
    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
    bool operator==(const RemoveSpellRequest& acRhs) const noexcept
    {
        return Opcode == acRhs.Opcode && TargetId == acRhs.TargetId && TargetOwnershipEpoch == acRhs.TargetOwnershipEpoch && SpellId == acRhs.SpellId;
    }

    uint32_t TargetId{};
    uint32_t TargetOwnershipEpoch{};
    GameId SpellId{};
};
