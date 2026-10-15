#include <Messages/RequestQuestSnapshot.h>
#include <TiltedCore/Serialization.hpp>

void RequestQuestSnapshot::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, QuestRevision);
    QuestContent.Serialize(aWriter);
}

void RequestQuestSnapshot::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    QuestRevision = Serialization::ReadVarInt(aReader);
    QuestContent = {};
    QuestContent.Deserialize(aReader);
}
