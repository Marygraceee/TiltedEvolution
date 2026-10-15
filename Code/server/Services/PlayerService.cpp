#include "Events/CharacterInteriorCellChangeEvent.h"
#include "Events/CharacterExteriorCellChangeEvent.h"
#include "Events/PlayerLeaveCellEvent.h"

#include <Services/PlayerService.h>
#include <Services/CharacterService.h>
#include <Services/InventoryService.h>
#include <GameServer.h>

#include <Messages/ShiftGridCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/AssignObjectsResponse.h>
#include <algorithm>

#include <Setting.h>
namespace
{
Console::Setting fGoldLossFactor{"Gameplay:fGoldLossFactor", "How much of their gold players lose on death, from 0 to 1 (e.g. 0.05 loses 5%, 0 loses nothing)", 0.0f};

void UpdateCharacterVisibility(
    World& aWorld, Player* apPlayer, const CellIdComponent& acPreviousCell, const CellIdComponent& acCurrentCell, const ShiftGridCellRequest* apGridShift) noexcept
{
    const bool hasNewlyLoadedCells = apGridShift && !apGridShift->Cells.empty();
    if (acPreviousCell.IsInInteriorCell() && acCurrentCell.IsInInteriorCell())
    {
        if (acPreviousCell.Cell == acCurrentCell.Cell)
            return;
    }
    else if (acPreviousCell.WorldSpaceId == acCurrentCell.WorldSpaceId && acPreviousCell.CenterCoords == acCurrentCell.CenterCoords && !hasNewlyLoadedCells)
        return;

    auto characterView = aWorld.view<CellIdComponent, CharacterComponent, OwnerComponent>();
    for (const auto character : characterView)
    {
        if (characterView.get<OwnerComponent>(character).GetOwner() == apPlayer)
            continue;

        const auto& characterCell = characterView.get<CellIdComponent>(character);
        const auto& characterComponent = characterView.get<CharacterComponent>(character);
        // An actor's parent cell can be temporary and absent from the client's cell list.
        // Use the same worldspace/position range as movement broadcasts, including dragon range.
        const auto transition = CellIdComponent::GetRangeTransition(acPreviousCell, acCurrentCell, characterCell, characterComponent.IsDragon());
        if (transition == CellIdComponent::RangeTransition::None)
        {
            if (!hasNewlyLoadedCells || !acCurrentCell.IsInRange(characterCell, characterComponent.IsDragon()))
                continue;

            const auto cellIt = std::find(apGridShift->Cells.begin(), apGridShift->Cells.end(), characterCell.CenterCoords);
            if (cellIt == apGridShift->Cells.end())
                continue;
        }

        if (transition == CellIdComponent::RangeTransition::Left)
        {
            NotifyRemoveCharacter removeMessage{};
            removeMessage.ServerId = World::ToInteger(character);
            apPlayer->Send(removeMessage);
            continue;
        }

        CharacterSpawnRequest spawnMessage;
        CharacterService::Serialize(aWorld, character, &spawnMessage);
        apPlayer->Send(spawnMessage);
    }
}
}

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_interiorCellEnterConnection(aDispatcher.sink<PacketEvent<EnterInteriorCellRequest>>().connect<&PlayerService::HandleInteriorCellEnter>(this))
    , m_gridCellShiftConnection(aDispatcher.sink<PacketEvent<ShiftGridCellRequest>>().connect<&PlayerService::HandleGridCellShift>(this))
    , m_exteriorCellEnterConnection(aDispatcher.sink<PacketEvent<EnterExteriorCellRequest>>().connect<&PlayerService::HandleExteriorCellEnter>(this))
    , m_playerRespawnConnection(aDispatcher.sink<PacketEvent<PlayerRespawnRequest>>().connect<&PlayerService::OnPlayerRespawnRequest>(this))
    , m_playerLevelConnection(aDispatcher.sink<PacketEvent<PlayerLevelRequest>>().connect<&PlayerService::OnPlayerLevelRequest>(this))
{
}

void SendPlayerCellChanged(const Player* apPlayer) noexcept
{
    auto& cellComponent = apPlayer->GetCellComponent();

    NotifyPlayerCellChanged notify{};
    notify.PlayerId = apPlayer->GetId();
    notify.WorldSpaceId = cellComponent.WorldSpaceId;
    notify.CellId = cellComponent.Cell;

    GameServer::Get()->SendToPlayers(notify, apPlayer);
}

void PlayerService::HandleGridCellShift(const PacketEvent<ShiftGridCellRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;
    auto& message = acMessage.Packet;
    const auto previousCell = pPlayer->GetCellComponent();
    const CellIdComponent currentCell{message.PlayerCell, message.WorldSpaceId, message.CenterCoords};

    pPlayer->SetCellComponent(currentCell);
    if (const auto character = pPlayer->GetCharacter())
    {
        if (m_world.all_of<CellIdComponent>(*character))
            m_world.replace<CellIdComponent>(*character, currentCell);

        const bool isDragon = m_world.get<CharacterComponent>(*character).IsDragon();
        for (Player* otherPlayer : m_world.GetPlayerManager())
        {
            if (otherPlayer == pPlayer)
                continue;

            const auto transition = CellIdComponent::GetRangeTransition(previousCell, currentCell, otherPlayer->GetCellComponent(), isDragon);
            if (transition == CellIdComponent::RangeTransition::None)
                continue;

            if (transition == CellIdComponent::RangeTransition::Left)
            {
                NotifyRemoveCharacter removeMessage{};
                removeMessage.ServerId = World::ToInteger(*character);
                otherPlayer->Send(removeMessage);
                continue;
            }

            CharacterSpawnRequest spawnMessage;
            CharacterService::Serialize(m_world, *character, &spawnMessage);
            otherPlayer->Send(spawnMessage);
        }
    }

    m_world.GetDispatcher().trigger(PlayerLeaveCellEvent(previousCell.Cell));
    UpdateCharacterVisibility(m_world, pPlayer, previousCell, currentCell, &message);

    AssignObjectsResponse droppedItems{};
    droppedItems.CellId = currentCell.Cell;
    droppedItems.WorldSpaceId = currentCell.WorldSpaceId;
    droppedItems.CurrentCoords = currentCell.CenterCoords;
    m_world.ctx().at<InventoryService>().AppendDroppedItems(droppedItems, currentCell, pPlayer->GetId());
    pPlayer->Send(droppedItems);
}

void PlayerService::HandleExteriorCellEnter(const PacketEvent<EnterExteriorCellRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;
    auto* pPlayer = acMessage.pPlayer;

    if (pPlayer->GetCharacter())
    {
        const auto previousCell = pPlayer->GetCellComponent();
        const auto entity = *pPlayer->GetCharacter();
        const CellIdComponent currentCell{message.CellId, message.WorldSpaceId, message.CurrentCoords};

        pPlayer->SetCellComponent(currentCell);
        if (m_world.all_of<CellIdComponent>(entity))
            m_world.replace<CellIdComponent>(entity, currentCell);

        if (previousCell)
            m_world.GetDispatcher().trigger(CharacterExteriorCellChangeEvent{pPlayer, entity, message.WorldSpaceId, message.CurrentCoords});

        UpdateCharacterVisibility(m_world, pPlayer, previousCell, currentCell, nullptr);
        SendPlayerCellChanged(pPlayer);
    }
}

void PlayerService::HandleInteriorCellEnter(const PacketEvent<EnterInteriorCellRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;
    auto& message = acMessage.Packet;
    const auto previousCell = pPlayer->GetCellComponent();
    const CellIdComponent currentCell{message.CellId, {}, {}};

    pPlayer->SetCellComponent(currentCell);
    m_world.GetDispatcher().trigger(PlayerLeaveCellEvent(previousCell.Cell));

    if (pPlayer->GetCharacter())
    {
        const auto entity = *pPlayer->GetCharacter();
        if (m_world.all_of<CellIdComponent>(entity))
        {
            m_world.replace<CellIdComponent>(entity, currentCell);
            m_world.GetDispatcher().trigger(CharacterInteriorCellChangeEvent{pPlayer, entity, message.CellId});
        }
    }

    UpdateCharacterVisibility(m_world, pPlayer, previousCell, currentCell, nullptr);
    SendPlayerCellChanged(pPlayer);
}

void PlayerService::OnPlayerRespawnRequest(const PacketEvent<PlayerRespawnRequest>& acMessage) const noexcept
{
    float goldLossFactor = fGoldLossFactor.as_float();

    auto character = acMessage.pPlayer->GetCharacter();
    if (!character)
        return;

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(*character));

    if (it != view.end())
    {
        if (goldLossFactor != 0.0)
        {
            auto& inventoryComponent = view.get<InventoryComponent>(*it);

            GameId goldId(0, 0xF);
            int32_t goldCount = inventoryComponent.Content.GetEntryCountById(goldId);
            int32_t goldToRemove = static_cast<int32_t>(goldCount * goldLossFactor);

            Inventory::Entry entry{};
            entry.BaseId = goldId;
            entry.Count = -goldToRemove;

            inventoryComponent.Content.AddOrRemoveEntry(entry);

            NotifyInventoryChanges notifyInventoryChanges{};
            notifyInventoryChanges.ServerId = World::ToInteger(*character);
            if (const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*character))
                notifyInventoryChanges.OwnershipEpoch = pOwnerComponent->OwnershipEpoch;
            notifyInventoryChanges.Item = entry;
            notifyInventoryChanges.Drop = false;

            // Exclude respawned player from inventory changes notification...
            if (!GameServer::Get()->SendToPlayersInRange(notifyInventoryChanges, *character, acMessage.GetSender()))
                spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

            // ...and instead, send NotifyPlayerRespawn so that the client can print a message.
            NotifyPlayerRespawn notifyPlayerRespawn{};
            notifyPlayerRespawn.GoldLost = goldToRemove;

            acMessage.pPlayer->Send(notifyPlayerRespawn);
        }

        // Let all other players in cell respawn this player, since the body state seems to be bugged otherwise
        NotifyRespawn notifyRespawn{};
        notifyRespawn.ActorId = World::ToInteger(*character);

        if (!GameServer::Get()->SendToPlayersInRange(notifyRespawn, *character, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
}

void PlayerService::OnPlayerLevelRequest(const PacketEvent<PlayerLevelRequest>& acMessage) const noexcept
{
    acMessage.pPlayer->SetLevel(acMessage.Packet.NewLevel);

    NotifyPlayerLevel notify{};
    notify.PlayerId = acMessage.pPlayer->GetId();
    notify.NewLevel = acMessage.Packet.NewLevel;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}
