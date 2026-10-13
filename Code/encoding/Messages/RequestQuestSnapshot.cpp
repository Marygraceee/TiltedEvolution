#include <Messages/RequestQuestSnapshot.h>

void RequestQuestSnapshot::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    QuestContent.Serialize(aWriter);
}

void RequestQuestSnapshot::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    QuestContent = {};
    QuestContent.Deserialize(aReader);
}
