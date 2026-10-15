#include <Messages/RequestInventoryChanges.h>
#include <TiltedCore/Serialization.hpp>

void RequestInventoryChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, OwnershipEpoch);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, DropId);
    Serialization::WriteVarInt(aWriter, ReferenceFormId);
    Serialization::WriteBool(aWriter, HasSenderCell);
    if (HasSenderCell)
    {
        SenderCellId.Serialize(aWriter);
        SenderWorldSpaceId.Serialize(aWriter);
        SenderCurrentCoords.Serialize(aWriter);
    }
    Serialization::WriteBool(aWriter, HasDroppedObject);
    if (HasDroppedObject)
        DroppedObject.Serialize(aWriter);
    Serialization::WriteBool(aWriter, Drop);
    Serialization::WriteBool(aWriter, UpdateClients);
}

void RequestInventoryChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    OwnershipEpoch = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    DropId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ReferenceFormId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    HasSenderCell = Serialization::ReadBool(aReader);
    if (HasSenderCell)
    {
        SenderCellId.Deserialize(aReader);
        SenderWorldSpaceId.Deserialize(aReader);
        SenderCurrentCoords.Deserialize(aReader);
    }
    HasDroppedObject = Serialization::ReadBool(aReader);
    if (HasDroppedObject)
        DroppedObject.Deserialize(aReader);
    Drop = Serialization::ReadBool(aReader);
    UpdateClients = Serialization::ReadBool(aReader);
}
