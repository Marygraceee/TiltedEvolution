#include <Messages/NotifyQuestSnapshot.h>

void NotifyQuestSnapshot::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    QuestContent.Serialize(aWriter);
}

void NotifyQuestSnapshot::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    QuestContent = {};
    QuestContent.Deserialize(aReader);
}
