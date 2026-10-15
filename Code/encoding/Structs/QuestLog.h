#pragma once

#include <cstdint>
#include <Structs/GameId.h>

using TiltedPhoques::Vector;

struct QuestLog
{
    enum ObjectiveFlags : uint8_t
    {
        ObjectiveDisplayed = 1 << 0,
        ObjectiveCompleted = 1 << 1,
        ObjectiveFailed = 1 << 2
    };

    enum QuestFlags : uint8_t
    {
        QuestCompleted = 1 << 0,
        QuestFailed = 1 << 1,
        QuestStopped = 1 << 2
    };

    static constexpr uint8_t ObjectiveFlagsMask = ObjectiveDisplayed | ObjectiveCompleted | ObjectiveFailed;
    static constexpr uint8_t QuestFlagsMask = QuestCompleted | QuestFailed | QuestStopped;
    static constexpr uint8_t ClientQuestFlagsShift = 5;
    static constexpr uint8_t ClientQuestFlagsMask = QuestFlagsMask << ClientQuestFlagsShift;

    static constexpr uint8_t EncodeClientQuestFlags(const uint8_t aFlags) noexcept { return (aFlags & QuestFlagsMask) << ClientQuestFlagsShift; }
    static constexpr uint8_t DecodeClientQuestFlags(const uint8_t aFlags) noexcept { return (aFlags & ClientQuestFlagsMask) >> ClientQuestFlagsShift; }

    struct Objective
    {
        uint16_t Index;
        uint8_t Flags;

        bool operator==(const Objective& acRhs) const noexcept { return Index == acRhs.Index && Flags == acRhs.Flags; }

        bool operator!=(const Objective& acRhs) const noexcept { return !this->operator==(acRhs); }
    };

    struct Entry
    {
        GameId Id;
        uint16_t Stage;
        Vector<Objective> Objectives{};
        uint8_t StateFlags{};

        bool operator==(const Entry& acRhs) const noexcept { return Id == acRhs.Id && Stage == acRhs.Stage && Objectives == acRhs.Objectives && StateFlags == acRhs.StateFlags; }

        bool operator!=(const Entry& acRhs) const noexcept { return !this->operator==(acRhs); }
    };

    Vector<Entry> Entries{};

    QuestLog() = default;
    ~QuestLog() = default;

    bool operator==(const QuestLog& acRhs) const noexcept;
    bool operator!=(const QuestLog& acRhs) const noexcept;
    bool IsValid() const noexcept;
    bool SetStage(GameId aId, uint16_t aStage);
    bool SetQuestFlags(GameId aId, uint8_t aFlags);
    bool SetObjectiveState(GameId aId, uint16_t aIndex, uint8_t aFlags);
    void Remove(GameId aId);

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
