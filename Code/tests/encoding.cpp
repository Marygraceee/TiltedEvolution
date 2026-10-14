#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <cstdint>
#include <limits>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "StringCache.h"
#include "Messages/StringCacheUpdate.h"

#include <catch2/catch.hpp>

#include <Messages/ClientMessageFactory.h>
#include <Messages/ServerMessageFactory.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/AssignObjectsRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestQuestSnapshot.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyQuestSnapshot.h>
#include <Structs/Vector2_NetQuantize.h>
#include <Structs/GridCellCoords.h>
#include <Structs/ObjectData.h>
#include <Structs/Movement.h>

#include <TiltedCore/Math.hpp>
#include <TiltedCore/Platform.hpp>

using namespace TiltedPhoques;

TEST_CASE("Movement ownership epochs round trip", "[encoding.movement]")
{
    Movement sent{};
    sent.OwnershipEpoch = 271;

    Buffer buffer(1000);
    Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    Movement received{};
    Buffer::Reader reader(&buffer);
    received.Deserialize(reader);

    REQUIRE(received == sent);
}

TEST_CASE("Grid cell ranges handle integer boundaries", "[encoding.grid_cell]")
{
    const int32_t minimum = std::numeric_limits<int32_t>::min();
    const int32_t maximum = std::numeric_limits<int32_t>::max();
    const GridCellCoords minimumCell{minimum, minimum};
    const GridCellCoords maximumCell{maximum, maximum};

    REQUIRE(GridCellCoords::IsCellInGridCell({minimum + 2, minimum + 2}, minimumCell, false));
    REQUIRE(GridCellCoords::IsCellInGridCell({maximum - 2, maximum - 2}, maximumCell, false));
    REQUIRE_FALSE(GridCellCoords::IsCellInGridCell({maximum, minimum}, minimumCell, false));
    REQUIRE(GridCellCoords::AreGridCellsOverlapping(minimumCell, {minimum + 4, minimum + 4}));
    REQUIRE(GridCellCoords::AreGridCellsOverlapping(maximumCell, {maximum - 4, maximum - 4}));
    REQUIRE_FALSE(GridCellCoords::AreGridCellsOverlapping(minimumCell, {maximum, minimum}));
}

TEST_CASE("Encoding factory", "[encoding.factory]")
{
    Buffer buff(1000);

    {
        AuthenticationRequest request;
        request.Token = "TesSt";

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<AuthenticationRequest>(std::move(pMessage));
        REQUIRE(pRequest->Token == request.Token);
    }

    {
        PartyAcceptInviteRequest request;
        request.InviterId = 123456;

        Buffer::Writer writer(&buff);
        request.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ClientMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == request.GetOpcode());

        auto pRequest = CastUnique<PartyAcceptInviteRequest>(std::move(pMessage));
        REQUIRE(pRequest->InviterId == request.InviterId);
    }
}

TEST_CASE("Dropped item assignment encoding", "[encoding.dropped_item]")
{
    ObjectData droppedItem{};
    droppedItem.ServerId = 17;
    droppedItem.Id = GameId{4, 900};
    droppedItem.CellId = GameId{0, 0x1234};
    droppedItem.WorldSpaceId = GameId{0, 0x5678};
    droppedItem.CurrentCoords = {12, -7};
    droppedItem.Position = glm::vec3{140.5f, 230.25f, 40.f};
    droppedItem.Rotation.x = 1.25f;
    droppedItem.Rotation.y = 2.5f;
    droppedItem.LocalReferenceId = 0xFF001234;
    droppedItem.IsDroppedItem = true;

    Inventory::Entry item{};
    item.BaseId = droppedItem.Id;
    item.Count = 3;
    droppedItem.CurrentInventory.Entries.push_back(item);

    AssignObjectsResponse response{};
    response.CellId = droppedItem.CellId;
    response.WorldSpaceId = droppedItem.WorldSpaceId;
    response.CurrentCoords = droppedItem.CurrentCoords;
    response.Objects.push_back(droppedItem);

    Buffer buff(1000);
    Buffer::Writer writer(&buff);
    response.Serialize(writer);

    Buffer::Reader reader(&buff);
    const ServerMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);

    auto decoded = CastUnique<AssignObjectsResponse>(std::move(message));
    REQUIRE(decoded->Objects.size() == 1);
    REQUIRE(decoded->Objects.front() == droppedItem);

    RequestInventoryChanges request{};
    request.ServerId = 11;
    request.OwnershipEpoch = 5;
    request.Item = item;
    request.Item.Count = -item.Count;
    request.ReferenceFormId = droppedItem.LocalReferenceId;
    request.DroppedObject = droppedItem;
    request.HasDroppedObject = true;
    request.Drop = true;

    Buffer requestBuffer(1000);
    Buffer::Writer requestWriter(&requestBuffer);
    request.Serialize(requestWriter);

    Buffer::Reader requestReader(&requestBuffer);
    const ClientMessageFactory clientFactory;
    auto requestMessage = clientFactory.Extract(requestReader);
    REQUIRE(requestMessage);
    auto decodedRequest = CastUnique<RequestInventoryChanges>(std::move(requestMessage));
    REQUIRE(*decodedRequest == request);

    NotifyInventoryChanges notify{};
    notify.ServerId = 11;
    notify.OwnershipEpoch = 5;
    notify.Item = request.Item;
    notify.DroppedObject = droppedItem;
    notify.HasDroppedObject = true;
    notify.Drop = true;

    Buffer notifyBuffer(1000);
    Buffer::Writer notifyWriter(&notifyBuffer);
    notify.Serialize(notifyWriter);

    Buffer::Reader notifyReader(&notifyBuffer);
    auto notifyMessage = factory.Extract(notifyReader);
    REQUIRE(notifyMessage);
    auto decodedNotify = CastUnique<NotifyInventoryChanges>(std::move(notifyMessage));
    REQUIRE(*decodedNotify == notify);
}

TEST_CASE("Dropped item assignment encoding supports a full session snapshot", "[encoding.dropped_item]")
{
    AssignObjectsResponse response{};
    for (uint32_t dropId = 1; dropId <= 256; ++dropId)
    {
        ObjectData droppedItem{};
        droppedItem.ServerId = dropId;
        droppedItem.Id = GameId{0, dropId};
        droppedItem.IsDroppedItem = true;
        response.Objects.push_back(std::move(droppedItem));
    }

    Buffer buff(100000);
    Buffer::Writer writer(&buff);
    response.Serialize(writer);

    Buffer::Reader reader(&buff);
    const ServerMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);

    auto decoded = CastUnique<AssignObjectsResponse>(std::move(message));
    REQUIRE(decoded->Objects.size() == 256);
    REQUIRE(decoded->Objects.back().ServerId == 256);
}

TEST_CASE("Object assignment request encoding supports a full cell", "[encoding.objects]")
{
    AssignObjectsRequest request{};
    request.CellId = GameId{0, 0x1234};
    request.WorldSpaceId = GameId{0, 0x5678};
    request.CurrentCoords = {12, -7};
    for (uint32_t objectId = 1; objectId <= 256; ++objectId)
    {
        ObjectData object{};
        object.Id = GameId{0, objectId};
        request.Objects.push_back(std::move(object));
    }

    Buffer buff(100000);
    Buffer::Writer writer(&buff);
    request.Serialize(writer);

    Buffer::Reader reader(&buff);
    const ClientMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);

    auto decoded = CastUnique<AssignObjectsRequest>(std::move(message));
    REQUIRE(*decoded == request);
    REQUIRE(decoded->Objects.size() == 256);
}

TEST_CASE("Character spawn encoding preserves dragon range", "[encoding.character]")
{
    CharacterSpawnRequest request{};
    request.ServerId = 17;
    request.OwnershipEpoch = 2;
    request.IsDragon = true;

    Buffer buff(10000);
    Buffer::Writer writer(&buff);
    request.Serialize(writer);

    Buffer::Reader reader(&buff);
    const ServerMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);

    auto decoded = CastUnique<CharacterSpawnRequest>(std::move(message));
    REQUIRE(decoded->IsDragon);
    REQUIRE(*decoded == request);
}

TEST_CASE("Quest snapshot encoding", "[encoding.quest_snapshot]")
{
    RequestQuestSnapshot request{};
    request.QuestContent.Entries.push_back({GameId{2, 0x1234}, 25});
    request.QuestContent.Entries.push_back({GameId{3, 0x5678}, 40});

    Buffer requestBuffer(1000);
    Buffer::Writer requestWriter(&requestBuffer);
    request.Serialize(requestWriter);

    Buffer::Reader requestReader(&requestBuffer);
    const ClientMessageFactory clientFactory;
    auto requestMessage = clientFactory.Extract(requestReader);
    REQUIRE(requestMessage);
    auto decodedRequest = CastUnique<RequestQuestSnapshot>(std::move(requestMessage));
    REQUIRE(*decodedRequest == request);

    NotifyQuestSnapshot notify{};
    notify.QuestContent = request.QuestContent;

    Buffer notifyBuffer(1000);
    Buffer::Writer notifyWriter(&notifyBuffer);
    notify.Serialize(notifyWriter);

    Buffer::Reader notifyReader(&notifyBuffer);
    const ServerMessageFactory serverFactory;
    auto notifyMessage = serverFactory.Extract(notifyReader);
    REQUIRE(notifyMessage);
    auto decodedNotify = CastUnique<NotifyQuestSnapshot>(std::move(notifyMessage));
    REQUIRE(*decodedNotify == notify);
}

TEST_CASE("Static structures", "[encoding.static]")
{
    GIVEN("GameId")
    {
        GameId sendObjects, recvObjects;
        sendObjects.ModId = 1456987;
        sendObjects.BaseId = 0x789654;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector3_NetQuantize")
    {
        Vector3_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 142.56f;
        sendObjects.y = 45687.7f;
        sendObjects.z = -142.56f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Vector2_NetQuantize")
    {
        Vector2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1000.89f;
        sendObjects.y = -485632.75f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize")
    {
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = 1.89f;
        sendObjects.y = TiltedPhoques::Pi * 2.0f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }

    GIVEN("Rotator2_NetQuantize needing wrap")
    {
        // This test is a bit dangerous as floating errors can lead to sendObjects != recvObjects but the difference is minuscule so we don't care abut such cases
        Rotator2_NetQuantize sendObjects, recvObjects;
        sendObjects.x = -1.87f;
        sendObjects.y = static_cast<float>(TiltedPhoques::Pi) * 18.0f + 3.6f;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendObjects.Serialize(writer);

            Buffer::Reader reader(&buff);
            recvObjects.Deserialize(reader);

            REQUIRE(sendObjects == recvObjects);
        }
    }
}

TEST_CASE("Differential structures", "[encoding.differential]")
{
    GIVEN("Full ActionEvent")
    {
        ActionEvent sendAction, recvAction;

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("A single cached event name")
    {
        ActionEvent sendAction, recvAction;

        TP_UNUSED(StringCache::Get().Add("test"))

        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.Type = 4;

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }

        {
            Buffer buff(1000);
            Buffer::Writer writer(&buff);

            sendAction.EventName = "Plot twist !";

            sendAction.GenerateDifferential(recvAction, writer);

            Buffer::Reader reader(&buff);
            recvAction.ApplyDifferential(reader);

            REQUIRE(sendAction == recvAction);
        }
    }

    GIVEN("Full Mods")
    {
        Mods sendMods, recvMods;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);

        sendMods.ModList.push_back({"Hello", 42});
        sendMods.ModList.push_back({"Hi", 14});
        sendMods.ModList.push_back({"Test", 8});
        sendMods.ModList.push_back({"Toast", 49});

        sendMods.Serialize(writer);

        Buffer::Reader reader(&buff);
        recvMods.Deserialize(reader);

        REQUIRE(sendMods == recvMods);
    }

    GIVEN("AnimationVariables")
    {
        AnimationVariables vars, recvVars;
 
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF"
                          "\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        Buffer buff(1000);
        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }

        vars.Booleans.resize(33);
        vars.Booleans[16] = false;
        vars.Booleans[17] = false;
        vars.Booleans[18] = false;
        vars.Booleans[19] = false;
        vars.Floats[3] = 42.f;
        vars.Integers[0] = 18;
        vars.Integers[3] = 0;

        {
            Buffer::Writer writer(&buff);

            vars.GenerateDiff(recvVars, writer);

            Buffer::Reader reader(&buff);
            recvVars.ApplyDiff(reader);

            REQUIRE(vars.Booleans == recvVars.Booleans);
            REQUIRE(vars.Floats == recvVars.Floats);
            REQUIRE(vars.Integers == recvVars.Integers);
        }
    }
}

TEST_CASE("Packets", "[encoding.packets]")
{
    SECTION("AuthenticationRequest")
    {
        Buffer buff(1000);

        AuthenticationRequest sendMessage, recvMessage;
        sendMessage.Token = "TesSt";
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AuthenticationResponse")
    {
        Buffer buff(1000);

        AuthenticationResponse sendMessage, recvMessage;
        sendMessage.Type = AuthenticationResponse::ResponseType::kAccepted;
        sendMessage.UserMods.ModList.push_back({"Hello", 42});
        sendMessage.UserMods.ModList.push_back({"Hi", 14});
        sendMessage.UserMods.ModList.push_back({"Test", 8});
        sendMessage.UserMods.ModList.push_back({"Toast", 49});

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    SECTION("AssignCharacterRequest")
    {
        Buffer buff(1000);

        ActionEvent sendAction;
        sendAction.ActionId = 42;
        sendAction.State1 = 6547;
        sendAction.Tick = 48;
        sendAction.ActorId = 12345678;
        sendAction.EventName = "test";
        sendAction.IdleId = 87964;
        sendAction.State2 = 8963;
        sendAction.TargetEventName = "toast";
        sendAction.TargetId = 963741;
        sendAction.Type = 4;

        AssignCharacterRequest sendMessage, recvMessage;
        sendMessage.Cookie = 14523698;
        sendMessage.AppearanceBuffer = "toto";
        sendMessage.CellId.BaseId = 45;
        sendMessage.FormId.ModId = 48;
        sendMessage.ReferenceId.BaseId = 456799;
        sendMessage.ReferenceId.ModId = 4079;
        sendMessage.LatestAction = sendAction;
        sendMessage.Position.x = -452.4f;
        sendMessage.Position.y = 452.4f;
        sendMessage.Position.z = 125452.4f;
        sendMessage.Rotation.x = -1.87f;
        sendMessage.Rotation.y = 45.35f;

        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    GIVEN("ClientReferencesMoveRequest")
    {
        ClientReferencesMoveRequest sendMessage, recvMessage;
        sendMessage.Tick = 1712345678901;
        auto& move = sendMessage.Updates[1];
        move.Position.x = 1234.5f;
        move.Direction = 0.75f;

        AnimationVariables vars;
        vars.Booleans.resize(76);
        String testString("\xDE\xAD\xBE\xEF\x76\xB");
        vars.String_to_VectorBool(testString, vars.Booleans);

        vars.Floats.push_back(1.f);
        vars.Floats.push_back(7.f);
        vars.Floats.push_back(12.f);
        vars.Floats.push_back(0.f);
        vars.Floats.push_back(145.f);
        vars.Floats.push_back(100.f);
        vars.Floats.push_back(-1.f);

        vars.Integers.push_back(0);
        vars.Integers.push_back(12000);
        vars.Integers.push_back(06);
        vars.Integers.push_back(7778);
        vars.Integers.push_back(41104539);

        move.Variables = vars;

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(recvMessage.Tick == sendMessage.Tick);
        REQUIRE(recvMessage.Updates[1] == sendMessage.Updates[1]);
    }

    GIVEN("RequestActionEvents")
    {
        ActionEvent firstAction;
        firstAction.Tick = 1712345678901;
        firstAction.ActionId = 42;
        firstAction.TargetId = 963741;
        firstAction.State1 = 6547;
        firstAction.EventName = "jumpStandingStart";

        ActionEvent secondAction = firstAction;
        secondAction.Tick += 120;
        secondAction.ActionId = 43;
        secondAction.EventName = "attackStart";
        secondAction.TargetEventName = "toast";

        RequestActionEvents sendMessage, recvMessage;
        sendMessage.Actions[1] = {firstAction, secondAction};
        sendMessage.Actions[7] = {secondAction};

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        recvMessage.DeserializeRaw(reader);

        REQUIRE(sendMessage == recvMessage);
    }

    GIVEN("NotifyActionEvents")
    {
        ActionEvent action;
        action.Tick = 1712345678901;
        action.ActionId = 42;
        action.EventName = "blockStart";

        NotifyActionEvents sendMessage;
        sendMessage.Actions[3] = {action, action};

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        sendMessage.Serialize(writer);

        Buffer::Reader reader(&buff);

        const ServerMessageFactory factory;
        auto pMessage = factory.Extract(reader);

        REQUIRE(pMessage);
        REQUIRE(pMessage->GetOpcode() == sendMessage.GetOpcode());
        REQUIRE(*CastUnique<NotifyActionEvents>(std::move(pMessage)) == sendMessage);
    }
}

TEST_CASE("StringCache", "[encoding.string_cache]")
{
    SECTION("Messages")
    {
        StringCacheUpdate update;
        update.Values.push_back("Hello");
        update.Values.push_back("Bye");

        Buffer buff(1000);
        Buffer::Writer writer(&buff);
        update.Serialize(writer);

        Buffer::Reader reader(&buff);

        uint64_t trash;
        reader.ReadBits(trash, 8); // pop opcode

        StringCacheUpdate recvUpdate;
        recvUpdate.DeserializeRaw(reader);

        REQUIRE(update == recvUpdate);
    }
}
