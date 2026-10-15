#include <Messages/RequestHealthChangeBroadcast.h>

void RequestHealthChangeBroadcast::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Id);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    Serialization::WriteFloat(aWriter, DeltaHealth);
    Serialization::WriteBool(aWriter, HasCurrentHealth);
    if (HasCurrentHealth)
        Serialization::WriteFloat(aWriter, CurrentHealth);
}

void RequestHealthChangeBroadcast::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    DeltaHealth = Serialization::ReadFloat(aReader);
    HasCurrentHealth = Serialization::ReadBool(aReader);
    CurrentHealth = HasCurrentHealth ? Serialization::ReadFloat(aReader) : 0.f;
}
