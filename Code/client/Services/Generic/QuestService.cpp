#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>

#include <Services/QuestService.h>
#include <Services/PapyrusService.h>
#include <Services/ImguiService.h>
#include <Services/OverlayService.h>
#include <Services/TransportService.h>

#include <OverlayApp.hpp>

#include <PlayerCharacter.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>

#include <Events/EventDispatcher.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/RequestQuestSnapshot.h>
#include <Messages/NotifyQuestSnapshot.h>
#include <Messages/NotifyPartyInfo.h>

#include <cstddef>
#include <unordered_map>

using ObjectiveBoolQuery = PapyrusFunction<bool, TESQuest, int32_t>;
using ObjectiveBoolSetter = PapyrusFunction<void, TESQuest, int32_t, bool>;
using ObjectiveDisplayedSetter = PapyrusFunction<void, TESQuest, int32_t, bool, bool>;

static bool IsValidObjectiveFlags(const uint8_t aFlags)
{
    constexpr uint8_t knownFlags = QuestLog::ObjectiveDisplayed | QuestLog::ObjectiveCompleted | QuestLog::ObjectiveFailed;
    return (aFlags & ~knownFlags) == 0 && (aFlags & (QuestLog::ObjectiveCompleted | QuestLog::ObjectiveFailed)) != (QuestLog::ObjectiveCompleted | QuestLog::ObjectiveFailed);
}

static void ApplyRemoteObjectiveState(
    TESQuest* apQuest, const uint16_t aIndex, const uint8_t aFlags, const ObjectiveBoolQuery& acIsDisplayed, const ObjectiveBoolQuery& acIsCompleted,
    const ObjectiveBoolQuery& acIsFailed, const ObjectiveDisplayedSetter& acSetDisplayed, const ObjectiveBoolSetter& acSetCompleted, const ObjectiveBoolSetter& acSetFailed)
{
    if (!IsValidObjectiveFlags(aFlags))
        return;

    const int32_t index = aIndex;
    const bool displayed = (aFlags & QuestLog::ObjectiveDisplayed) != 0;
    const bool completed = (aFlags & QuestLog::ObjectiveCompleted) != 0;
    const bool failed = (aFlags & QuestLog::ObjectiveFailed) != 0;
    const bool wasDisplayed = acIsDisplayed(apQuest, index);
    const bool wasCompleted = acIsCompleted(apQuest, index);
    const bool wasFailed = acIsFailed(apQuest, index);

    if (wasCompleted && !completed)
        acSetCompleted(apQuest, index, false);
    if (wasFailed && !failed)
        acSetFailed(apQuest, index, false);
    if (wasDisplayed != displayed)
        acSetDisplayed(apQuest, index, displayed, false);
    if (!wasCompleted && completed)
        acSetCompleted(apQuest, index, true);
    if (!wasFailed && failed)
        acSetFailed(apQuest, index, true);
}

static void SendObjectiveStateUpdates(World& aWorld, TESQuest* apQuest, const GameId aQuestId)
{
    if (!aWorld.GetTransport().IsOnline() || !aWorld.GetPartyService().IsInParty())
        return;

    auto& papyrus = aWorld.ctx().at<PapyrusService>();
    const void* isObjectiveDisplayedAddress = papyrus.Get("Quest", "IsObjectiveDisplayed");
    const void* isObjectiveCompletedAddress = papyrus.Get("Quest", "IsObjectiveCompleted");
    const void* isObjectiveFailedAddress = papyrus.Get("Quest", "IsObjectiveFailed");
    if (!isObjectiveDisplayedAddress || !isObjectiveCompletedAddress || !isObjectiveFailedAddress)
        return;

    const ObjectiveBoolQuery isObjectiveDisplayed(isObjectiveDisplayedAddress);
    const ObjectiveBoolQuery isObjectiveCompleted(isObjectiveCompletedAddress);
    const ObjectiveBoolQuery isObjectiveFailed(isObjectiveFailedAddress);
    for (const auto& objective : apQuest->objectives)
    {
        if (!objective)
            continue;

        uint8_t flags = 0;
        if (isObjectiveDisplayed(apQuest, objective->stageId))
            flags |= QuestLog::ObjectiveDisplayed;
        if (isObjectiveCompleted(apQuest, objective->stageId))
            flags |= QuestLog::ObjectiveCompleted;
        if (isObjectiveFailed(apQuest, objective->stageId))
            flags |= QuestLog::ObjectiveFailed;

        RequestQuestUpdate update{};
        update.Id = aQuestId;
        update.Stage = objective->stageId;
        update.Status = RequestQuestUpdate::ObjectiveUpdate;
        update.ClientQuestType = flags;
        aWorld.GetTransport().Send(update);
    }
}

static void ApplyRemoteQuestStage(TESQuest* apQuest, const uint16_t acStage)
{
    if (apQuest->currentStage != acStage && (acStage < apQuest->currentStage || apQuest->IsStageDone(acStage)))
        apQuest->Reset();

    apQuest->ScriptSetStage(acStage);
}

static TESQuest* FindQuestByNameId(const String& name)
{
    auto& questRegistry = ModManager::Get()->quests;
    auto it = std::find_if(questRegistry.begin(), questRegistry.end(), [name](auto* it) { return std::strcmp(it->idName.AsAscii(), name.c_str()); });

    return it != questRegistry.end() ? *it : nullptr;
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_joinedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestService::OnConnected>(this);
    m_questUpdateConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&QuestService::OnQuestUpdate>(this);
    m_partyInfoConnection = aDispatcher.sink<NotifyPartyInfo>().connect<&QuestService::OnPartyInfo>(this);
    m_questSnapshotConnection = aDispatcher.sink<NotifyQuestSnapshot>().connect<&QuestService::OnQuestSnapshot>(this);

    // A note about the Gameevents:
    // TESQuestStageItemDoneEvent gets fired to late, we instead use TESQuestStageEvent, because it responds immediately.
    // TESQuestInitEvent can be instead managed by start stop quest management.
    // bind game event listeners
    auto* pEventList = EventDispatcherManager::Get();
    pEventList->questStartStopEvent.RegisterSink(this);
    pEventList->questStageEvent.RegisterSink(this);
}

void QuestService::OnConnected(const ConnectedEvent&) noexcept
{
    // TODO: this should be followed with whatever the quest leader selected
    /*
    // deselect any active quests
    auto* pPlayer = PlayerCharacter::Get();
    for (auto& objective : pPlayer->objectives)
    {
        if (auto* pQuest = objective.instance->quest)
            pQuest->SetActive(false);
    }
    */
}

BSTEventResult QuestService::OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*)
{
    if (ScopedQuestOverride::IsOverriden())
        return BSTEventResult::kOk;

    if (!m_world.Get().GetPartyService().IsInParty())
    {
        if (apEvent->started)
            NotifyOverlayOfQuestUpdate(apEvent->formId);
        return BSTEventResult::kOk;
    }

    spdlog::info("Quest start/stop event: {:X}", apEvent->formId);

    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;
     
        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            // Perhaps redundant, but necessary. We need the logging and
            // the lambda coming up is queued and runs later
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(),  pQuest->currentStage, pQuest->IsStopped() ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type), 
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }
        
        m_world.GetRunner().Queue(
            [&, formId = pQuest->formID, stageId = pQuest->currentStage, stopped = pQuest->IsStopped(), type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = stopped ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type); 

                    m_world.GetTransport().Send(update);

                    if (!stopped)
                    {
                        auto* currentQuest = Cast<TESQuest>(TESForm::GetById(formId));
                        if (currentQuest)
                            SendObjectiveStateUpdates(m_world, currentQuest, Id);
                    }
                }
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStageEvent* apEvent, const EventDispatcher<TESQuestStageEvent>*)
{
    if (ScopedQuestOverride::IsOverriden())
        return BSTEventResult::kOk;

    if (!m_world.Get().GetPartyService().IsInParty())
    {
        NotifyOverlayOfQuestUpdate(apEvent->formId);
        return BSTEventResult::kOk;
    }

    spdlog::info("Quest stage event: {:X}, stage: {}", apEvent->formId, apEvent->stageId);

    // there is no reason to even fetch the quest object, since the event provides everything already....
    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            // Perhaps redundant, but necessary. We need the logging and
            // the lambda coming up is queued and runs later
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(), pQuest->currentStage,
                             RequestQuestUpdate::StageUpdate,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().Queue(
            [&, formId = apEvent->formId, stageId = apEvent->stageId, type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = RequestQuestUpdate::StageUpdate;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type);

                    m_world.GetTransport().Send(update);
                    auto* currentQuest = Cast<TESQuest>(TESForm::GetById(formId));
                    if (currentQuest)
                        SendObjectiveStateUpdates(m_world, currentQuest, Id);
                }
            });
    }

    return BSTEventResult::kOk;
}

void QuestService::OnQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept
{
    if (aUpdate.Status == NotifyQuestUpdate::ObjectiveUpdate)
    {
        if (!IsValidObjectiveFlags(aUpdate.ClientQuestType))
            return;

        ModSystem& modSystem = World::Get().GetModSystem();
        const uint32_t formId = modSystem.GetGameId(aUpdate.Id);
        TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
        if (!pQuest || IsNonSyncableQuest(pQuest))
            return;

        auto& papyrus = World::Get().ctx().at<PapyrusService>();
        const void* isObjectiveDisplayedAddress = papyrus.Get("Quest", "IsObjectiveDisplayed");
        const void* isObjectiveCompletedAddress = papyrus.Get("Quest", "IsObjectiveCompleted");
        const void* isObjectiveFailedAddress = papyrus.Get("Quest", "IsObjectiveFailed");
        const void* setObjectiveDisplayedAddress = papyrus.Get("Quest", "SetObjectiveDisplayed");
        const void* setObjectiveCompletedAddress = papyrus.Get("Quest", "SetObjectiveCompleted");
        const void* setObjectiveFailedAddress = papyrus.Get("Quest", "SetObjectiveFailed");
        if (!isObjectiveDisplayedAddress || !isObjectiveCompletedAddress || !isObjectiveFailedAddress || !setObjectiveDisplayedAddress || !setObjectiveCompletedAddress ||
            !setObjectiveFailedAddress)
        {
            spdlog::error("Quest objective Papyrus functions are unavailable; skipping objective update");
            return;
        }

        const ObjectiveBoolQuery isObjectiveDisplayed(isObjectiveDisplayedAddress);
        const ObjectiveBoolQuery isObjectiveCompleted(isObjectiveCompletedAddress);
        const ObjectiveBoolQuery isObjectiveFailed(isObjectiveFailedAddress);
        const ObjectiveDisplayedSetter setObjectiveDisplayed(setObjectiveDisplayedAddress);
        const ObjectiveBoolSetter setObjectiveCompleted(setObjectiveCompletedAddress);
        const ObjectiveBoolSetter setObjectiveFailed(setObjectiveFailedAddress);
        ScopedQuestOverride questOverride;
        if (!pQuest->IsActive())
            pQuest->SetActive(true);
        ApplyRemoteObjectiveState(
            pQuest, aUpdate.Stage, aUpdate.ClientQuestType, isObjectiveDisplayed, isObjectiveCompleted, isObjectiveFailed, setObjectiveDisplayed, setObjectiveCompleted,
            setObjectiveFailed);
        return;
    }

    ModSystem& modSystem = World::Get().GetModSystem();
    uint32_t formId = modSystem.GetGameId(aUpdate.Id);
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
    if (!pQuest)
    {
        spdlog::error("Failed to find quest, base id: {:X}, mod id: {:X}", aUpdate.Id.BaseId, aUpdate.Id.ModId);
        return;
    }

    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
    {
        spdlog::info(__FUNCTION__ ": receiving type none/misc quest update gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status,
                     aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
    }

    ScopedQuestOverride questOverride;
    bool bResult = false;
    switch (aUpdate.Status)
    {
    case NotifyQuestUpdate::Started:
    {
        ApplyRemoteQuestStage(pQuest, aUpdate.Stage);
        pQuest->SetActive(true);
        bResult = true;
        spdlog::info("Remote quest started: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    }
    case NotifyQuestUpdate::StageUpdate:
        ApplyRemoteQuestStage(pQuest, aUpdate.Stage);
        if (!pQuest->IsActive())
            pQuest->SetActive(true);
        bResult = true;
        spdlog::info("Remote quest updated: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    case NotifyQuestUpdate::Stopped:
        bResult = StopQuest(formId);
        spdlog::info("Remote quest stopped: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    default: break;
    }

    if (!bResult)
        spdlog::error("Failed to update the client quest state, quest: {:X}, stage: {}, status: {}", formId, aUpdate.Stage, aUpdate.Status);
}

void QuestService::OnPartyInfo(const NotifyPartyInfo& acPartyInfo) noexcept
{
    if (!acPartyInfo.IsLeader)
        return;

    auto& papyrus = m_world.ctx().at<PapyrusService>();
    const void* isObjectiveDisplayedAddress = papyrus.Get("Quest", "IsObjectiveDisplayed");
    const void* isObjectiveCompletedAddress = papyrus.Get("Quest", "IsObjectiveCompleted");
    const void* isObjectiveFailedAddress = papyrus.Get("Quest", "IsObjectiveFailed");
    if (!isObjectiveDisplayedAddress || !isObjectiveCompletedAddress || !isObjectiveFailedAddress)
    {
        spdlog::error("Quest objective query functions are unavailable; skipping quest snapshot");
        return;
    }

    const ObjectiveBoolQuery isObjectiveDisplayed(isObjectiveDisplayedAddress);
    const ObjectiveBoolQuery isObjectiveCompleted(isObjectiveCompletedAddress);
    const ObjectiveBoolQuery isObjectiveFailed(isObjectiveFailedAddress);
    RequestQuestSnapshot request{};
    auto& modSystem = m_world.GetModSystem();
    std::unordered_map<GameId, std::size_t> entriesByQuestId;
    entriesByQuestId.reserve(ModManager::Get()->quests.length);
    for (TESQuest* pQuest : ModManager::Get()->quests)
    {
        if (!pQuest || pQuest->IsStopped() || IsNonSyncableQuest(pQuest))
            continue;

        GameId questId{};
        if (!modSystem.GetServerModId(pQuest->formID, questId))
            continue;

        const auto [entry, inserted] = entriesByQuestId.emplace(questId, request.QuestContent.Entries.size());
        if (inserted)
        {
            request.QuestContent.Entries.push_back({questId, pQuest->currentStage, {}});
            auto& questEntry = request.QuestContent.Entries.back();
            for (const auto& objective : pQuest->objectives)
            {
                uint8_t flags = 0;
                if (isObjectiveDisplayed(pQuest, objective->stageId))
                    flags |= QuestLog::ObjectiveDisplayed;
                if (isObjectiveCompleted(pQuest, objective->stageId))
                    flags |= QuestLog::ObjectiveCompleted;
                if (isObjectiveFailed(pQuest, objective->stageId))
                    flags |= QuestLog::ObjectiveFailed;
                questEntry.Objectives.push_back({objective->stageId, flags});
            }
        }
        else
            request.QuestContent.Entries[entry->second].Stage = pQuest->currentStage;
    }

    m_world.GetTransport().Send(request);
}

void QuestService::OnQuestSnapshot(const NotifyQuestSnapshot& acSnapshot) noexcept
{
    ModSystem& modSystem = m_world.GetModSystem();
    std::unordered_map<uint32_t, const QuestLog::Entry*> entriesByFormId;
    for (const auto& entry : acSnapshot.QuestContent.Entries)
    {
        const uint32_t formId = modSystem.GetGameId(entry.Id);
        TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
        if (pQuest && !IsNonSyncableQuest(pQuest))
            entriesByFormId[formId] = &entry;
    }

    auto& papyrus = m_world.ctx().at<PapyrusService>();
    const void* isObjectiveDisplayedAddress = papyrus.Get("Quest", "IsObjectiveDisplayed");
    const void* isObjectiveCompletedAddress = papyrus.Get("Quest", "IsObjectiveCompleted");
    const void* isObjectiveFailedAddress = papyrus.Get("Quest", "IsObjectiveFailed");
    const void* setObjectiveDisplayedAddress = papyrus.Get("Quest", "SetObjectiveDisplayed");
    const void* setObjectiveCompletedAddress = papyrus.Get("Quest", "SetObjectiveCompleted");
    const void* setObjectiveFailedAddress = papyrus.Get("Quest", "SetObjectiveFailed");
    const bool canApplyObjectives = isObjectiveDisplayedAddress && isObjectiveCompletedAddress && isObjectiveFailedAddress && setObjectiveDisplayedAddress &&
                                    setObjectiveCompletedAddress && setObjectiveFailedAddress;
    if (!canApplyObjectives)
        spdlog::error("Quest objective Papyrus functions are unavailable; applying quest stages only");

    const ObjectiveBoolQuery isObjectiveDisplayed(isObjectiveDisplayedAddress);
    const ObjectiveBoolQuery isObjectiveCompleted(isObjectiveCompletedAddress);
    const ObjectiveBoolQuery isObjectiveFailed(isObjectiveFailedAddress);
    const ObjectiveDisplayedSetter setObjectiveDisplayed(setObjectiveDisplayedAddress);
    const ObjectiveBoolSetter setObjectiveCompleted(setObjectiveCompletedAddress);
    const ObjectiveBoolSetter setObjectiveFailed(setObjectiveFailedAddress);
    ScopedQuestOverride questOverride;
    for (TESQuest* pQuest : ModManager::Get()->quests)
    {
        if (!pQuest || IsNonSyncableQuest(pQuest))
            continue;

        const auto entryIt = entriesByFormId.find(pQuest->formID);
        if (entryIt == entriesByFormId.end())
        {
            if (!pQuest->IsStopped())
                StopQuest(pQuest->formID);
            continue;
        }

        const auto& questEntry = *entryIt->second;
        if (pQuest->currentStage != questEntry.Stage || pQuest->IsStopped())
            ApplyRemoteQuestStage(pQuest, questEntry.Stage);
        if (!pQuest->IsActive())
            pQuest->SetActive(true);

        if (!canApplyObjectives)
            continue;

        for (const auto& objective : questEntry.Objectives)
            ApplyRemoteObjectiveState(
                pQuest, objective.Index, objective.Flags, isObjectiveDisplayed, isObjectiveCompleted, isObjectiveFailed, setObjectiveDisplayed, setObjectiveCompleted,
                setObjectiveFailed);
    }
}

void QuestService::NotifyOverlayOfQuestUpdate(uint32_t aFormId) noexcept
{
    if (!m_world.GetTransport().IsOnline())
        return;

    // Nobody around to party with, or the invite toast already prompts the player to join one.
    const auto remotePlayerView = m_world.view<PlayerComponent, RemoteComponent>();
    if (m_world.GetPartyService().HasPendingInvitations() || remotePlayerView.begin() == remotePlayerView.end())
        return;

    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aFormId));
    if (!pQuest || IsNonSyncableQuest(pQuest))
        return;

    // Hidden background quests and minor misc tasks are too noisy to prompt about
    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        return;

    m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("questUpdated");
}

bool QuestService::StopQuest(uint32_t aformId)
{
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aformId));
    if (pQuest)
    {
        pQuest->SetActive(false);
        pQuest->SetStopped();
        return true;
    }

    return false;
}

static constexpr std::array kNonSyncableQuestIds = std::to_array<uint32_t>({
    0x2BA16,   // Werewolf transformation quest
    0x20071D0, // Vampire transformation quest
    0x3AC44,   // MS13BleakFallsBarrowLeverScene
    // 0xFE014801,  // Unknown dynamic ID, kept as note, maybe lookup correct ID this game?
    0xF2593 // Skill experience quest
});

bool QuestService::IsNonSyncableQuest(TESQuest* apQuest)
{
    // Quests with no quest stages are never synced. Most TESQues::Type:: quests should
    // be synced, including Type::None and Type::Miscellaneous, but there are a few
    // known exceptions that should be excluded that are in the table.
    return    apQuest->stages.Empty() 
           || std::find(kNonSyncableQuestIds.begin(), kNonSyncableQuestIds.end(), apQuest->formID) != kNonSyncableQuestIds.end();
}

void QuestService::DebugDumpQuests()
{
    auto& quests = ModManager::Get()->quests;
    for (TESQuest* pQuest : quests)
        spdlog::info("{:X}|{}|{}|{}", pQuest->formID, (uint8_t)pQuest->type, pQuest->priority, pQuest->idName.AsAscii());
}
