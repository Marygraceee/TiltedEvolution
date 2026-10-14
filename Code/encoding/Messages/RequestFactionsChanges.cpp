#include <Messages/RequestFactionsChanges.h>
#include <TiltedCore/Serialization.hpp>
#include <cassert>

void RequestFactionsChanges::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    assert(Changes.size() < 0x100);

    aWriter.WriteBits(Changes.size() & 0xFF, 8);

    for (auto& change : Changes)
    {
        Serialization::WriteVarInt(aWriter, change.first);
        const auto epochIt = OwnershipEpochs.find(change.first);
        Serialization::WriteVarInt(aWriter, epochIt != OwnershipEpochs.end() ? epochIt->second : 0);
        change.second.Serialize(aWriter);
    }
}

void RequestFactionsChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t count = 0;
    aReader.ReadBits(count, 8);

    for (auto i = 0u; i < count; ++i)
    {
        const uint32_t id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        OwnershipEpochs[id] = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
        auto& change = Changes[id];
        change.Deserialize(aReader);
    }
}
