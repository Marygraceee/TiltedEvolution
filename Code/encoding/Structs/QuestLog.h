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

        bool operator==(const Entry& acRhs) const noexcept { return Id == acRhs.Id && Stage == acRhs.Stage && Objectives == acRhs.Objectives; }

        bool operator!=(const Entry& acRhs) const noexcept { return !this->operator==(acRhs); }
    };

    Vector<Entry> Entries{};

    QuestLog() = default;
    ~QuestLog() = default;

    bool operator==(const QuestLog& acRhs) const noexcept;
    bool operator!=(const QuestLog& acRhs) const noexcept;
    bool SetStage(GameId aId, uint16_t aStage);
    bool SetObjectiveState(GameId aId, uint16_t aIndex, uint8_t aFlags);
    void Remove(GameId aId);

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;
};
