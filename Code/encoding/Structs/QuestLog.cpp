
#include <Structs/QuestLog.h>

#include <TiltedCore/Serialization.hpp>
#include <Structs/Mods.h>
#include <algorithm>
#include <limits>
#include <utility>

using TiltedPhoques::Serialization;

bool QuestLog::operator==(const QuestLog& acRhs) const noexcept
{
    return Entries == acRhs.Entries;
}

bool QuestLog::operator!=(const QuestLog& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

bool QuestLog::IsValid() const noexcept
{
    if (Entries.size() > std::numeric_limits<uint16_t>::max())
        return false;

    for (const auto& entry : Entries)
    {
        if ((entry.StateFlags & ~QuestFlagsMask) != 0 || entry.Objectives.size() > std::numeric_limits<uint16_t>::max())
            return false;

        for (const auto& objective : entry.Objectives)
        {
            if ((objective.Flags & ~ObjectiveFlagsMask) != 0 || (objective.Flags & (ObjectiveCompleted | ObjectiveFailed)) == (ObjectiveCompleted | ObjectiveFailed))
                return false;
        }
    }

    return true;
}

bool QuestLog::SetStage(const GameId aId, const uint16_t aStage)
{
    const auto questIt = std::find_if(Entries.begin(), Entries.end(), [aId](const auto& entry) { return entry.Id == aId; });
    if (questIt != Entries.end() && questIt->Stage == aStage)
        return false;
    if (questIt == Entries.end() && Entries.size() >= std::numeric_limits<uint16_t>::max())
        return false;

    Vector<Objective> objectives;
    uint8_t stateFlags = 0;
    if (questIt != Entries.end())
    {
        objectives = questIt->Objectives;
        stateFlags = questIt->StateFlags;
        Remove(aId);
    }

    Entries.push_back({aId, aStage, std::move(objectives), stateFlags});
    return true;
}

bool QuestLog::SetQuestFlags(const GameId aId, const uint8_t aFlags)
{
    if ((aFlags & ~QuestFlagsMask) != 0)
        return false;

    const auto questIt = std::find_if(Entries.begin(), Entries.end(), [aId](const auto& entry) { return entry.Id == aId; });
    if (questIt == Entries.end() || questIt->StateFlags == aFlags)
        return false;

    questIt->StateFlags = aFlags;
    return true;
}

bool QuestLog::SetObjectiveState(const GameId aId, const uint16_t aIndex, const uint8_t aFlags)
{
    const auto questIt = std::find_if(Entries.begin(), Entries.end(), [aId](const auto& entry) { return entry.Id == aId; });
    if (questIt == Entries.end() || (aFlags & ~ObjectiveFlagsMask) != 0 || (aFlags & (ObjectiveCompleted | ObjectiveFailed)) == (ObjectiveCompleted | ObjectiveFailed))
        return false;

    const auto objectiveIt = std::find_if(questIt->Objectives.begin(), questIt->Objectives.end(), [aIndex](const auto& objective) { return objective.Index == aIndex; });
    if (objectiveIt != questIt->Objectives.end())
    {
        if (objectiveIt->Flags == aFlags)
            return false;

        objectiveIt->Flags = aFlags;
        return true;
    }
    if (questIt->Objectives.size() >= std::numeric_limits<uint16_t>::max())
        return false;

    questIt->Objectives.push_back({aIndex, aFlags});
    return true;
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
        aWriter.WriteBits(e.StateFlags, 8);
        aWriter.WriteBits(e.Objectives.size() & 0xFFFF, 16);
        for (const auto& objective : e.Objectives)
        {
            aWriter.WriteBits(objective.Index, 16);
            aWriter.WriteBits(objective.Flags, 8);
        }
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
        aReader.ReadBits(temp, 8);
        e.StateFlags = temp & 0xFF;
        aReader.ReadBits(temp, 16);
        e.Objectives.resize(temp & 0xFFFF);
        for (auto& objective : e.Objectives)
        {
            aReader.ReadBits(temp, 16);
            objective.Index = temp & 0xFFFF;
            aReader.ReadBits(temp, 8);
            objective.Flags = temp & 0xFF;
        }
    }
}
