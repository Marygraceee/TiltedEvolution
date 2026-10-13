#include <Messages/AssignObjectsRequest.h>
#include <TiltedCore/Serialization.hpp>

void AssignObjectsRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    CurrentCoords.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, Objects.size());

    for (const auto& object : Objects)
    {
        object.Serialize(aWriter);
    }
}

void AssignObjectsRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    CurrentCoords.Deserialize(aReader);

    const uint64_t count = Serialization::ReadVarInt(aReader);

    Objects.resize(count);

    for (auto i = 0u; i < count; ++i)
    {
        Objects[i].Deserialize(aReader);
    }
}
