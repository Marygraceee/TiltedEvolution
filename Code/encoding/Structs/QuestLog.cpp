
#include <Structs/QuestLog.h>

#include <TiltedCore/Serialization.hpp>
#include <Structs/Mods.h>
#include <algorithm>
#include <limits>

using TiltedPhoques::Serialization;

bool QuestLog::operator==(const QuestLog& acRhs) const noexcept
{
    return Entries == acRhs.Entries;
}

bool QuestLog::operator!=(const QuestLog& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void QuestLog::SetStage(const GameId aId, const uint16_t aStage)
{
    const auto questIt = std::find_if(Entries.begin(), Entries.end(), [aId](const auto& entry) { return entry.Id == aId; });
    if (questIt == Entries.end() && Entries.size() >= std::numeric_limits<uint16_t>::max())
        return;

    Remove(aId);
    Entries.push_back({aId, aStage});
}

void QuestLog::Remove(const GameId aId)
{
    Entries.erase(std::remove_if(Entries.begin(), Entries.end(), [aId](const auto& entry) { return entry.Id == aId; }), Entries.end());
}

void QuestLog::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(Entries.size() & 0xFFFF, 16);
    for (auto& e : Entries)
    {
        e.Id.Serialize(aWriter);

        // id's can be in the full 16 bit range
        aWriter.WriteBits(e.Stage, 16);
    }
}

void QuestLog::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t temp;
    aReader.ReadBits(temp, 16);

    const size_t count = temp & 0xFFFF;
    Entries.resize(count);

    for (auto& e : Entries)
    {
        e.Id.Deserialize(aReader);
        aReader.ReadBits(temp, 16);

        e.Stage = temp & 0xFFFF;
    }
}
