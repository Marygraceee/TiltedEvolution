#include <GameServer.h>
#include <Components.h>

#include <World.h>
#include <Services/QuestService.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/RequestQuestSnapshot.h>
#include <Messages/NotifyQuestSnapshot.h>
#include <limits>

static bool HasTerminalQuestFlag(const uint8_t aFlags)
{
    return (aFlags & (QuestLog::QuestCompleted | QuestLog::QuestFailed)) != 0;
}

static void ApplyQuestChange(QuestLog& aQuestLog, GameId aId, uint16_t aStage, uint8_t aStatus, uint8_t aStateFlags);

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
    if (!acMessage.Packet.QuestContent.IsValid())
        return;

    if (pParty->HasQuestSnapshot && acMessage.Packet.QuestRevision != pParty->QuestRevision)
    {
        NotifyQuestSnapshot notify{};
        notify.QuestContent = pParty->QuestContent;
        pPlayer->Send(notify);
        return;
    }

    if (!pParty->HasQuestSnapshot && acMessage.Packet.QuestRevision > pParty->QuestRevision)
        return;

    if (!pParty->HasQuestSnapshot)
    {
        const QuestLog pendingObjectiveContent = pParty->QuestContent;
        pParty->QuestContent = acMessage.Packet.QuestContent;
        pParty->HasQuestSnapshot = true;
        for (const auto& [questId, change] : pParty->PendingQuestUpdates)
        {
            ApplyQuestChange(pParty->QuestContent, questId, change.Stage, change.Status, change.StateFlags);
        }
        for (const auto& entry : pendingObjectiveContent.Entries)
        {
            if (!pParty->PendingQuestUpdates.contains(entry.Id))
                pParty->QuestContent.SetQuestFlags(entry.Id, entry.StateFlags);
            for (const auto& objective : entry.Objectives)
                pParty->QuestContent.SetObjectiveState(entry.Id, objective.Index, objective.Flags);
        }
        pParty->PendingQuestUpdates.clear();
    }
    else
        pParty->QuestContent = acMessage.Packet.QuestContent;

    ++pParty->QuestRevision;

    for (Player* pMember : pParty->Members)
        pMember->GetQuestLogComponent().QuestContent = pParty->QuestContent;

    NotifyQuestSnapshot notify{};
    notify.QuestContent = pParty->QuestContent;
    GameServer::Get()->SendToParty(notify, pPlayer->GetParty());
}

void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;
    Player* pPlayer = acMessage.pPlayer;
    auto& partyService = m_world.GetPartyService();
    auto* pParty = partyService.GetPlayerParty(pPlayer);

    if (message.Status == RequestQuestUpdate::ObjectiveUpdate)
    {
        constexpr uint8_t knownObjectiveFlags = QuestLog::ObjectiveFlagsMask | QuestLog::ClientQuestFlagsMask;
        const uint8_t objectiveFlags = message.ClientQuestType & QuestLog::ObjectiveFlagsMask;
        const uint8_t questFlags = QuestLog::DecodeClientQuestFlags(message.ClientQuestType);
        if ((message.ClientQuestType & ~knownObjectiveFlags) != 0 ||
            (objectiveFlags & (QuestLog::ObjectiveCompleted | QuestLog::ObjectiveFailed)) == (QuestLog::ObjectiveCompleted | QuestLog::ObjectiveFailed))
            return;

        auto& senderContent = pPlayer->GetQuestLogComponent().QuestContent;
        auto senderQuestIt = std::find_if(senderContent.Entries.begin(), senderContent.Entries.end(), [&message](const auto& entry) { return entry.Id == message.Id; });
        if (senderQuestIt == senderContent.Entries.end())
            return;

        QuestLog& questContent = pParty ? pParty->QuestContent : senderContent;
        uint16_t questStage = senderQuestIt->Stage;
        auto questIt = std::find_if(questContent.Entries.begin(), questContent.Entries.end(), [&message](const auto& entry) { return entry.Id == message.Id; });
        if (pParty && !pParty->HasQuestSnapshot)
        {
            const auto pendingIt = pParty->PendingQuestUpdates.find(message.Id);
            if (pendingIt == pParty->PendingQuestUpdates.end() && pParty->PendingQuestUpdates.size() >= std::numeric_limits<uint16_t>::max())
                return;
        }

        if (pParty && pParty->HasQuestSnapshot)
        {
            if (questIt == questContent.Entries.end())
                return;
            questStage = questIt->Stage;
        }
        else if (questIt == questContent.Entries.end() || questIt->Stage != questStage)
        {
            if (!questContent.SetStage(message.Id, questStage))
                return;
        }

        questIt = std::find_if(questContent.Entries.begin(), questContent.Entries.end(), [&message](const auto& entry) { return entry.Id == message.Id; });

        if (pParty && !pParty->HasQuestSnapshot)
        {
            const auto pendingIt = pParty->PendingQuestUpdates.find(message.Id);
            uint8_t status = pendingIt == pParty->PendingQuestUpdates.end() ? RequestQuestUpdate::StageUpdate : pendingIt->second.Status;
            if (questFlags & QuestLog::QuestStopped)
                status = RequestQuestUpdate::Stopped;
            else if (status == RequestQuestUpdate::Stopped)
                status = RequestQuestUpdate::StageUpdate;

            pParty->PendingQuestUpdates[message.Id] = {message.Id, questStage, status, questFlags};
        }

        bool changed = false;
        if ((questFlags & QuestLog::QuestStopped) && !HasTerminalQuestFlag(questFlags))
        {
            changed = questIt != questContent.Entries.end();
            questContent.Remove(message.Id);
        }
        else
        {
            changed = questContent.SetObjectiveState(message.Id, message.Stage, objectiveFlags);
            changed = questContent.SetQuestFlags(message.Id, questFlags) || changed;
        }

        if (!changed)
            return;

        if (pParty)
            ++pParty->QuestRevision;

        if (!pParty)
        {
            return;
        }

        for (Player* pMember : pParty->Members)
        {
            if (pParty->HasQuestSnapshot)
                pMember->GetQuestLogComponent().QuestContent = pParty->QuestContent;
            else
            {
                auto& memberContent = pMember->GetQuestLogComponent().QuestContent;
                if ((questFlags & QuestLog::QuestStopped) && !HasTerminalQuestFlag(questFlags))
                    memberContent.Remove(message.Id);
                else
                {
                    memberContent.SetStage(message.Id, questStage);
                    memberContent.SetObjectiveState(message.Id, message.Stage, objectiveFlags);
                    memberContent.SetQuestFlags(message.Id, questFlags);
                }
            }
        }

        NotifyQuestUpdate notify{};
        notify.Id = message.Id;
        notify.Stage = message.Stage;
        notify.Status = NotifyQuestUpdate::ObjectiveUpdate;
        notify.ClientQuestType = objectiveFlags | QuestLog::EncodeClientQuestFlags(questFlags);
        GameServer::Get()->SendToParty(notify, pPlayer->GetParty(), acMessage.GetSender());
        return;
    }

    if (message.Status > RequestQuestUpdate::Stopped)
        return;

    if ((message.ClientQuestType & (1 << 4)) || (message.ClientQuestType & 0x0F) > 11)
        return;

    uint8_t stateFlags = QuestLog::DecodeClientQuestFlags(message.ClientQuestType);
    const bool isStopped = message.Status == RequestQuestUpdate::Stopped || (stateFlags & QuestLog::QuestStopped);
    if (isStopped)
        stateFlags |= QuestLog::QuestStopped;
    const bool retainStoppedQuest = !isStopped || HasTerminalQuestFlag(stateFlags);

    bool hasQuest = false;
    if (pParty)
    {
        if (pParty->HasQuestSnapshot)
        {
            const auto questIt =
                std::find_if(pParty->QuestContent.Entries.begin(), pParty->QuestContent.Entries.end(), [&message](const auto& entry) { return entry.Id == message.Id; });
            hasQuest = questIt != pParty->QuestContent.Entries.end();
            if (hasQuest && questIt->Stage == message.Stage && questIt->StateFlags == stateFlags)
                return;
            if (isStopped && !retainStoppedQuest && !hasQuest)
                return;
        }
        else
        {
            const auto pendingIt = pParty->PendingQuestUpdates.find(message.Id);
            hasQuest =
                pendingIt != pParty->PendingQuestUpdates.end() && (pendingIt->second.Status != RequestQuestUpdate::Stopped || HasTerminalQuestFlag(pendingIt->second.StateFlags));
            if (pendingIt != pParty->PendingQuestUpdates.end())
            {
                if (pendingIt->second.Stage == message.Stage && pendingIt->second.StateFlags == stateFlags &&
                    (pendingIt->second.Status == RequestQuestUpdate::Stopped) == isStopped)
                    return;
            }
            if (isStopped && !retainStoppedQuest && !hasQuest)
                return;
        }
    }
    else
    {
        const auto& entries = pPlayer->GetQuestLogComponent().QuestContent.Entries;
        const auto questIt = std::find_if(entries.begin(), entries.end(), [&message](const auto& entry) { return entry.Id == message.Id; });
        hasQuest = questIt != entries.end();
        if (hasQuest && questIt->Stage == message.Stage && questIt->StateFlags == stateFlags)
            return;
        if (isStopped && !retainStoppedQuest && !hasQuest)
            return;
    }

    if ((!pParty || pParty->HasQuestSnapshot) && retainStoppedQuest && !hasQuest)
    {
        const auto& entries = pParty ? pParty->QuestContent.Entries : pPlayer->GetQuestLogComponent().QuestContent.Entries;
        if (entries.size() >= std::numeric_limits<uint16_t>::max())
            return;
    }

    NotifyQuestUpdate notify{};
    notify.Id = message.Id;
    notify.Stage = message.Stage;
    if (isStopped)
        notify.Status = NotifyQuestUpdate::Stopped;
    else if (hasQuest)
        notify.Status = NotifyQuestUpdate::StageUpdate;
    else
        notify.Status = NotifyQuestUpdate::Started;
    notify.ClientQuestType = (message.ClientQuestType & ~QuestLog::ClientQuestFlagsMask) | QuestLog::EncodeClientQuestFlags(stateFlags);

    const uint8_t clientQuestType = notify.ClientQuestType & 0x0F;
    if (clientQuestType == 0 || clientQuestType == 6)
        spdlog::debug(
            "{}: syncing type none/misc quest to party, gameId {:X} questStage {} questStatus {} questType {}", __FUNCTION__, notify.Id.LogFormat(), notify.Stage, notify.Status,
            clientQuestType);

    if (pParty && !pParty->HasQuestSnapshot)
    {
        const auto pendingIt = pParty->PendingQuestUpdates.find(message.Id);
        if (pendingIt == pParty->PendingQuestUpdates.end() && pParty->PendingQuestUpdates.size() >= std::numeric_limits<uint16_t>::max())
            return;

        pParty->PendingQuestUpdates[message.Id] = {message.Id, message.Stage, static_cast<uint8_t>(isStopped ? RequestQuestUpdate::Stopped : message.Status), stateFlags};
    }
    else if (pParty)
    {
        ApplyQuestChange(pParty->QuestContent, message.Id, message.Stage, notify.Status, stateFlags);
    }
    else
        ApplyQuestChange(pPlayer->GetQuestLogComponent().QuestContent, message.Id, message.Stage, notify.Status, stateFlags);

    if (pParty)
        ++pParty->QuestRevision;

    if (!pParty)
        return;

    for (Player* pMember : pParty->Members)
    {
        if (pParty->HasQuestSnapshot)
            pMember->GetQuestLogComponent().QuestContent = pParty->QuestContent;
        else
            ApplyQuestChange(pMember->GetQuestLogComponent().QuestContent, message.Id, message.Stage, notify.Status, stateFlags);
    }

    GameServer::Get()->SendToParty(notify, pPlayer->GetParty(), acMessage.GetSender());
}

static void ApplyQuestChange(QuestLog& aQuestLog, const GameId aId, const uint16_t aStage, const uint8_t aStatus, uint8_t aStateFlags)
{
    const bool isStopped = aStatus == RequestQuestUpdate::Stopped || (aStateFlags & QuestLog::QuestStopped);
    if (isStopped)
        aStateFlags |= QuestLog::QuestStopped;

    if (isStopped && !HasTerminalQuestFlag(aStateFlags))
        aQuestLog.Remove(aId);
    else
    {
        // in order to prevent bugs when a quest is in progress
        // and being updated we add it as a new quest record to
        // maintain a proper remote questlog state.
        aQuestLog.SetStage(aId, aStage);
        aQuestLog.SetQuestFlags(aId, aStateFlags);
    }
}
