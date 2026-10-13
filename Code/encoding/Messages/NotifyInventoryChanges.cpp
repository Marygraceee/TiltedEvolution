#include <Messages/NotifyInventoryChanges.h>
#include <TiltedCore/Serialization.hpp>

void NotifyInventoryChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, DropId);
    Serialization::WriteBool(aWriter, HasDroppedObject);
    if (HasDroppedObject)
        DroppedObject.Serialize(aWriter);
    Serialization::WriteBool(aWriter, Drop);
}

void NotifyInventoryChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    DropId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HasDroppedObject = Serialization::ReadBool(aReader);
    if (HasDroppedObject)
        DroppedObject.Deserialize(aReader);
    Drop = Serialization::ReadBool(aReader);
}
