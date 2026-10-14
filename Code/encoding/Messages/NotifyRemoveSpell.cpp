#include "NotifyRemoveSpell.h"

void NotifyRemoveSpell::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, TargetId);
    Serialization::WriteVarInt(aWriter, TargetOwnershipEpoch);
    SpellId.Serialize(aWriter);
}

void NotifyRemoveSpell::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    TargetId = Serialization::ReadVarInt(aReader);
    TargetOwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    SpellId.Deserialize(aReader);
}
