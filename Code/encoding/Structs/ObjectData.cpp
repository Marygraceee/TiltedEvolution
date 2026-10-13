#include <Structs/ObjectData.h>
#include <TiltedCore/Serialization.hpp>

using TiltedPhoques::Serialization;

bool ObjectData::operator==(const ObjectData& acRhs) const noexcept
{
    return ServerId == acRhs.ServerId && Id == acRhs.Id && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && CurrentCoords == acRhs.CurrentCoords &&
           Position == acRhs.Position && Rotation == acRhs.Rotation && CurrentLockData == acRhs.CurrentLockData && CurrentInventory == acRhs.CurrentInventory &&
           LocalReferenceId == acRhs.LocalReferenceId && IsDroppedItem == acRhs.IsDroppedItem && IsSenderFirst == acRhs.IsSenderFirst;
}

bool ObjectData::operator!=(const ObjectData& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void ObjectData::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Id.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsDroppedItem);
    if (IsDroppedItem)
    {
        Position.Serialize(aWriter);
        Rotation.Serialize(aWriter);
        Serialization::WriteVarInt(aWriter, LocalReferenceId);
    }
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    CurrentCoords.Serialize(aWriter);
    CurrentLockData.Serialize(aWriter);
    CurrentInventory.Serialize(aWriter);
    Serialization::WriteBool(aWriter, IsSenderFirst);
}

void ObjectData::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Id.Deserialize(aReader);
    IsDroppedItem = Serialization::ReadBool(aReader);
    if (IsDroppedItem)
    {
        Position.Deserialize(aReader);
        Rotation.Deserialize(aReader);
        LocalReferenceId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    }
    else
    {
        Position.x = 0.f;
        Position.y = 0.f;
        Position.z = 0.f;
        Rotation.x = 0.f;
        Rotation.y = 0.f;
        LocalReferenceId = 0;
    }
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    CurrentCoords.Deserialize(aReader);
    CurrentLockData.Deserialize(aReader);
    CurrentInventory.Deserialize(aReader);
    IsSenderFirst = Serialization::ReadBool(aReader);
}
