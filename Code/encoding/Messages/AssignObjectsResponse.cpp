#include <Messages/AssignObjectsResponse.h>
#include <TiltedCore/Serialization.hpp>

void AssignObjectsResponse::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
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

void AssignObjectsResponse::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

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
