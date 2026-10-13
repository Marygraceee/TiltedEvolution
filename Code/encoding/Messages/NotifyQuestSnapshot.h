#pragma once

#include "Message.h"
#include <Structs/QuestLog.h>

struct NotifyQuestSnapshot final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyQuestSnapshot;

    NotifyQuestSnapshot()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyQuestSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && QuestContent == acRhs.QuestContent; }

    QuestLog QuestContent{};
};
