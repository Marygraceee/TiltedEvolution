#pragma once

#include "Message.h"
#include <Structs/QuestLog.h>

struct RequestQuestSnapshot final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestQuestSnapshot;

    RequestQuestSnapshot()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestQuestSnapshot& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && QuestContent == acRhs.QuestContent; }

    QuestLog QuestContent{};
};
