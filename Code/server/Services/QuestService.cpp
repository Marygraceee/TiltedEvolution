#include <GameServer.h>
#include <Components.h>

#include <World.h>
#include <Services/QuestService.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/RequestQuestSnapshot.h>
#include <Messages/NotifyQuestSnapshot.h>
#include <limits>

static void ApplyQuestChange(QuestLog& aQuestLog, GameId aId, uint16_t aStage, uint8_t aStatus);

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);
    m_questSnapshotConnection = aDispatcher.sink<PacketEvent<RequestQuestSnapshot>>().connect<&QuestService::OnQuestSnapshot>(this);
}

void QuestService::OnQuestSnapshot(const PacketEvent<RequestQuestSnapshot>& acMessage) noexcept
{
    Player* pPlayer = acMessage.pPlayer;
    auto& partyService = m_world.GetPartyService();
    if (!partyService.IsPlayerInParty(pPlayer) || !partyService.IsPlayerLeader(pPlayer))
        return;

    auto* pParty = partyService.GetPlayerParty(pPlayer);
    if (!pParty)
        return;

    if (!pParty->HasQuestSnapshot)
    {
        pParty->QuestContent = acMessage.Packet.QuestContent;
        pParty->HasQuestSnapshot = true;
        for (const auto& [questId, change] : pParty->PendingQuestUpdates)
        {
            ApplyQuestChange(pParty->QuestContent, questId, change.Stage, change.Status);
        }
        pParty->PendingQuestUpdates.clear();
    }
    else
        pParty->QuestContent = acMessage.Packet.QuestContent;

    for (Player* pMember : pParty->Members)
        pMember->GetQuestLogComponent().QuestContent = pParty->QuestContent;

    NotifyQuestSnapshot notify{};
    notify.QuestContent = pParty->QuestContent;
    GameServer::Get()->SendToParty(notify, pPlayer->GetParty());
}

void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    if (message.Status > RequestQuestUpdate::Stopped)
        return;

    Player* pPlayer = acMessage.pPlayer;
    auto& partyService = m_world.GetPartyService();
    auto* pParty = partyService.GetPlayerParty(pPlayer);

    bool hasQuest = false;
    if (pParty)
    {
        if (pParty->HasQuestSnapshot)
        {
            const auto questIt =
                std::find_if(pParty->QuestContent.Entries.begin(), pParty->QuestContent.Entries.end(), [&message](const auto& entry) { return entry.Id == message.Id; });
            hasQuest = questIt != pParty->QuestContent.Entries.end();
            if (message.Status != RequestQuestUpdate::Stopped && hasQuest && questIt->Stage == message.Stage)
                return;
        }
        else
        {
            const auto pendingIt = pParty->PendingQuestUpdates.find(message.Id);
            hasQuest = pendingIt != pParty->PendingQuestUpdates.end() && pendingIt->second.Status != RequestQuestUpdate::Stopped;
            if (pendingIt != pParty->PendingQuestUpdates.end())
            {
                if (message.Status == RequestQuestUpdate::Stopped && pendingIt->second.Status == RequestQuestUpdate::Stopped)
                    return;
                if (message.Status != RequestQuestUpdate::Stopped && hasQuest && pendingIt->second.Stage == message.Stage)
                    return;
            }
        }
    }
    else
    {
        const auto& entries = pPlayer->GetQuestLogComponent().QuestContent.Entries;
        const auto questIt = std::find_if(entries.begin(), entries.end(), [&message](const auto& entry) { return entry.Id == message.Id; });
        hasQuest = questIt != entries.end();
        if (message.Status != RequestQuestUpdate::Stopped && hasQuest && questIt->Stage == message.Stage)
            return;
    }

    if ((!pParty || pParty->HasQuestSnapshot) && message.Status != RequestQuestUpdate::Stopped && !hasQuest)
    {
        const auto& entries = pParty ? pParty->QuestContent.Entries : pPlayer->GetQuestLogComponent().QuestContent.Entries;
        if (entries.size() >= std::numeric_limits<uint16_t>::max())
            return;
    }

    NotifyQuestUpdate notify{};
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    if (message.Status == RequestQuestUpdate::Stopped)
        notify.Status = NotifyQuestUpdate::Stopped;
    else if (hasQuest)
        notify.Status = NotifyQuestUpdate::StageUpdate;
    else
        notify.Status = NotifyQuestUpdate::Started;
    notify.ClientQuestType = message.ClientQuestType;

    if (notify.ClientQuestType == 0 || notify.ClientQuestType == 6)
        spdlog::debug("{}: syncing type none/misc quest to party, gameId {:X} questStage {} questStatus {} questType {}",
                     __FUNCTION__, notify.Id.LogFormat(), notify.Stage, notify.Status, notify.ClientQuestType);

    if (pParty && !pParty->HasQuestSnapshot)
    {
        const auto pendingIt = pParty->PendingQuestUpdates.find(message.Id);
        if (pendingIt == pParty->PendingQuestUpdates.end() && pParty->PendingQuestUpdates.size() >= std::numeric_limits<uint16_t>::max())
            return;

        pParty->PendingQuestUpdates[message.Id] = {message.Id, message.Stage, message.Status};
    }
    else if (pParty)
    {
        ApplyQuestChange(pParty->QuestContent, message.Id, message.Stage, message.Status);
    }
    else
        ApplyQuestChange(pPlayer->GetQuestLogComponent().QuestContent, message.Id, message.Stage, message.Status);

    if (!pParty)
        return;

    for (Player* pMember : pParty->Members)
    {
        if (pParty->HasQuestSnapshot)
            pMember->GetQuestLogComponent().QuestContent = pParty->QuestContent;
        else
            ApplyQuestChange(pMember->GetQuestLogComponent().QuestContent, message.Id, message.Stage, message.Status);
    }

    GameServer::Get()->SendToParty(notify, pPlayer->GetParty(), acMessage.GetSender());
}

static void ApplyQuestChange(QuestLog& aQuestLog, const GameId aId, const uint16_t aStage, const uint8_t aStatus)
{
    if (aStatus == RequestQuestUpdate::Stopped)
        aQuestLog.Remove(aId);
    else
    {
        // in order to prevent bugs when a quest is in progress
        // and being updated we add it as a new quest record to
        // maintain a proper remote questlog state.
        aQuestLog.SetStage(aId, aStage);
    }
}
