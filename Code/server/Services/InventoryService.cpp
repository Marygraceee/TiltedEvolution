#include "InventoryService.h"

#include <Components.h>
#include <World.h>
#include <GameServer.h>

#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/AssignObjectsResponse.h>

#include <limits>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_inventoryChangeConnection = aDispatcher.sink<PacketEvent<RequestInventoryChanges>>().connect<&InventoryService::OnInventoryChanges>(this);
    m_equipmentChangeConnection = aDispatcher.sink<PacketEvent<RequestEquipmentChanges>>().connect<&InventoryService::OnEquipmentChanges>(this);
    m_drawWeaponConnection = aDispatcher.sink<PacketEvent<DrawWeaponRequest>>().connect<&InventoryService::OnWeaponDrawnRequest>(this);
}

void InventoryService::AppendDroppedItems(AssignObjectsResponse& aResponse, const CellIdComponent& acCell, const uint32_t aPlayerId) const noexcept
{
    const auto appendDropsAt = [this, &aResponse, aPlayerId](const DroppedItemCellKey& acCellKey)
    {
        const auto cellIt = m_droppedItemsByCell.find(acCellKey);
        if (cellIt == m_droppedItemsByCell.end())
            return;

        for (const entt::entity entity : cellIt->second)
        {
            if (!m_world.valid(entity))
                continue;

            const auto& droppedItem = m_world.get<DroppedItemComponent>(entity);
            ObjectData responseData = droppedItem.Data;
            if (droppedItem.OriginPlayerId != aPlayerId)
                responseData.LocalReferenceId = 0;
            aResponse.Objects.push_back(std::move(responseData));
        }
    };

    if (acCell.IsInInteriorCell())
    {
        appendDropsAt({acCell.Cell, {}, {}});
        return;
    }

    const int32_t distanceToBorder = GridCellCoords::m_gridsToLoad / 2;
    for (int32_t x = -distanceToBorder; x <= distanceToBorder; ++x)
    {
        const int64_t cellX = static_cast<int64_t>(acCell.CenterCoords.X) + x;
        if (cellX < std::numeric_limits<int32_t>::min() || cellX > std::numeric_limits<int32_t>::max())
            continue;

        for (int32_t y = -distanceToBorder; y <= distanceToBorder; ++y)
        {
            const int64_t cellY = static_cast<int64_t>(acCell.CenterCoords.Y) + y;
            if (cellY < std::numeric_limits<int32_t>::min() || cellY > std::numeric_limits<int32_t>::max())
                continue;

            appendDropsAt({{}, acCell.WorldSpaceId, {static_cast<int32_t>(cellX), static_cast<int32_t>(cellY)}});
        }
    }
}

void InventoryService::OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    const auto rejectDroppedItemDrop = [&]()
    {
        if (!message.Drop || message.Item.Count >= 0 || message.Item.Count == std::numeric_limits<int32_t>::min())
            return;

        NotifyInventoryChanges correction;
        correction.ServerId = message.ServerId;
        correction.OwnershipEpoch = message.OwnershipEpoch;
        correction.Item = message.Item;
        correction.Item.Count = -correction.Item.Count;
        correction.ReferenceFormId = message.ReferenceFormId;
        correction.DropRejected = true;
        acMessage.pPlayer->Send(correction);
    };

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(message.ServerId));

    if (it == view.end())
    {
        rejectDroppedItemDrop();
        return;
    }

    bool isRemoteNpcInteraction = false;

    const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*it);
    if (pOwnerComponent)
    {
        const auto* pOwner = pOwnerComponent->GetOwner();
        if (pOwnerComponent->OwnershipEpoch != message.OwnershipEpoch)
        {
            const uint32_t ownerId = pOwner ? pOwner->GetId() : 0;
            spdlog::debug(
                "Rejected inventory change from player {:X} for actor {:X} at stale epoch {}; current owner is {:X} at epoch {}",
                acMessage.pPlayer->GetId(), message.ServerId, message.OwnershipEpoch, ownerId, pOwnerComponent->OwnershipEpoch);
            rejectDroppedItemDrop();
            return;
        }

        if (pOwner != acMessage.pPlayer)
        {
            const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(*it);
            const auto* pCellComponent = m_world.try_get<CellIdComponent>(*it);
            // A non-owner may still change an NPC's inventory through normal gameplay interactions
            // such as pickpocketing or looting. The epoch and range checks keep the interaction tied
            // to the currently visible incarnation of that NPC.
            isRemoteNpcInteraction = pOwner && pCharacterComponent && pCellComponent && !pCharacterComponent->IsPlayer()
                && acMessage.pPlayer->GetCellComponent().IsInRange(*pCellComponent, pCharacterComponent->IsDragon());

            if (!isRemoteNpcInteraction)
            {
                const uint32_t ownerId = pOwner ? pOwner->GetId() : 0;
                spdlog::debug(
                    "Rejected inventory change from player {:X} for actor {:X} because it is owned by player {:X}", acMessage.pPlayer->GetId(), message.ServerId, ownerId);
                rejectDroppedItemDrop();
                return;
            }
        }
    }
    else if (message.OwnershipEpoch != 0)
    {
        spdlog::warn(
            "Rejected inventory change from player {:X} because object {:X} unexpectedly carried ownership epoch {}",
            acMessage.pPlayer->GetId(), message.ServerId, message.OwnershipEpoch);
        rejectDroppedItemDrop();
        return;
    }

    auto& inventoryComponent = view.get<InventoryComponent>(*it);

    auto droppedView = m_world.view<DroppedItemComponent>();
    auto droppedIt = droppedView.end();
    uint32_t dropId = message.DropId;
    if (dropId == 0 && !message.Drop && message.ReferenceFormId != 0)
    {
        const uint64_t referenceKey = GetDroppedItemReferenceKey(acMessage.pPlayer->GetId(), message.ReferenceFormId);
        const auto indexedEntity = m_droppedItemsByLocalReference.find(referenceKey);
        if (indexedEntity != m_droppedItemsByLocalReference.end() && m_world.valid(indexedEntity->second))
            droppedIt = droppedView.find(indexedEntity->second);
        else if (indexedEntity != m_droppedItemsByLocalReference.end())
            m_droppedItemsByLocalReference.erase(indexedEntity);

        if (droppedIt != droppedView.end())
            dropId = droppedView.get<DroppedItemComponent>(*droppedIt).Data.ServerId;
    }

    if (message.Drop && message.ReferenceFormId != 0)
    {
        const uint64_t referenceKey = GetDroppedItemReferenceKey(acMessage.pPlayer->GetId(), message.ReferenceFormId);
        const auto indexedEntity = m_droppedItemsByLocalReference.find(referenceKey);
        if (indexedEntity != m_droppedItemsByLocalReference.end())
        {
            if (m_world.valid(indexedEntity->second))
            {
                const auto existingDrop = droppedView.find(indexedEntity->second);
                if (existingDrop != droppedView.end())
                {
                    const auto& droppedItem = droppedView.get<DroppedItemComponent>(*existingDrop);
                    NotifyInventoryChanges notify;
                    notify.ServerId = message.ServerId;
                    notify.OwnershipEpoch = message.OwnershipEpoch;
                    notify.Item = message.Item;
                    notify.Item.Count = 0;
                    notify.DropId = droppedItem.Data.ServerId;
                    notify.DroppedObject = droppedItem.Data;
                    notify.HasDroppedObject = true;
                    notify.Drop = true;
                    notify.ReferenceFormId = message.ReferenceFormId;
                    acMessage.pPlayer->Send(notify);
                    return;
                }
            }
            m_droppedItemsByLocalReference.erase(indexedEntity);
        }
    }

    const auto rejectDroppedItemPickup = [&]()
    {
        if (message.Drop || message.Item.Count <= 0 || dropId == 0)
            return;

        NotifyInventoryChanges correction;
        correction.ServerId = message.ServerId;
        correction.OwnershipEpoch = message.OwnershipEpoch;
        correction.Item = message.Item;
        correction.Item.Count = -correction.Item.Count;
        correction.DropId = dropId;
        correction.HasDroppedObject = true;
        correction.DroppedObject.IsDroppedItem = true;
        correction.DroppedObject.ServerId = dropId;
        correction.DroppedObject.CurrentInventory.Entries.push_back(message.Item);
        correction.DroppedObject.CurrentInventory.Entries.back().Count = 0;

        if (droppedIt != droppedView.end())
        {
            const auto& droppedItem = droppedView.get<DroppedItemComponent>(*droppedIt);
            if (droppedItem.Data.CurrentInventory.Entries.size() == 1)
            {
                correction.DroppedObject = droppedItem.Data;
                if (droppedItem.OriginPlayerId != acMessage.pPlayer->GetId())
                    correction.DroppedObject.LocalReferenceId = 0;
            }
        }

        acMessage.pPlayer->Send(correction);
    };

    if (dropId != 0)
    {
        if (message.Item.Count <= 0)
            return;

        if (droppedIt == droppedView.end())
        {
            const auto indexedEntity = m_droppedItemEntities.find(dropId);
            if (indexedEntity != m_droppedItemEntities.end() && m_world.valid(indexedEntity->second))
                droppedIt = droppedView.find(indexedEntity->second);
            else if (indexedEntity != m_droppedItemEntities.end())
                m_droppedItemEntities.erase(indexedEntity);
        }

        if (droppedIt == droppedView.end())
        {
            rejectDroppedItemPickup();
            return;
        }

        const auto& droppedItem = droppedView.get<DroppedItemComponent>(*droppedIt);
        const auto& droppedData = droppedItem.Data;
        if (droppedData.CurrentInventory.Entries.size() != 1)
        {
            rejectDroppedItemPickup();
            return;
        }

        const auto& droppedEntry = droppedData.CurrentInventory.Entries.front();
        const CellIdComponent droppedCell{droppedData.CellId, droppedData.WorldSpaceId, droppedData.CurrentCoords};
        if (!droppedEntry.CanBeMerged(message.Item) || message.Item.Count > droppedEntry.Count || !acMessage.pPlayer->GetCellComponent().IsInRange(droppedCell, false))
        {
            rejectDroppedItemPickup();
            return;
        }
    }

    if (message.Drop && (message.Item.Count >= 0 || message.Item.Count == std::numeric_limits<int32_t>::min()))
        return;

    if (message.HasDroppedObject &&
        (!message.Drop || !message.DroppedObject.IsDroppedItem || message.ReferenceFormId == 0 || message.DroppedObject.LocalReferenceId != message.ReferenceFormId))
    {
        rejectDroppedItemDrop();
        return;
    }

    if (message.HasDroppedObject && message.HasSenderCell && message.SenderWorldSpaceId == GameId{} && message.SenderCellId == GameId{})
    {
        rejectDroppedItemDrop();
        return;
    }

    bool shouldCreateDroppedItem = message.Drop;
    ObjectData droppedObject = message.DroppedObject;
    if (shouldCreateDroppedItem)
    {
        if (!message.HasDroppedObject)
        {
            const auto* pCell = m_world.try_get<CellIdComponent>(*it);
            const auto* pMovement = m_world.try_get<MovementComponent>(*it);
            if (!pCell || !pMovement)
            {
                rejectDroppedItemDrop();
                return;
            }
            else
            {
                droppedObject.Id = message.Item.BaseId;
                droppedObject.CellId = pCell->Cell;
                droppedObject.WorldSpaceId = pCell->WorldSpaceId;
                droppedObject.CurrentCoords = GridCellCoords::CalculateGridCellCoords(pMovement->Position.x, pMovement->Position.y);
                droppedObject.Position = pMovement->Position;
                droppedObject.Rotation.x = pMovement->Rotation.x;
                droppedObject.Rotation.y = pMovement->Rotation.z;
                droppedObject.LocalReferenceId = message.ReferenceFormId;
                droppedObject.IsDroppedItem = true;
                auto droppedEntry = message.Item;
                droppedEntry.Count = -droppedEntry.Count;
                droppedEntry.ExtraWorn = false;
                droppedEntry.ExtraWornLeft = false;
                droppedObject.CurrentInventory.Entries.push_back(std::move(droppedEntry));
            }
        }

        if (shouldCreateDroppedItem && droppedObject.CurrentInventory.Entries.size() != 1)
        {
            rejectDroppedItemDrop();
            return;
        }

        if (shouldCreateDroppedItem)
        {
            const auto& droppedEntry = droppedObject.CurrentInventory.Entries.front();
            Inventory::Entry expectedDroppedEntry = message.Item;
            expectedDroppedEntry.Count = -message.Item.Count;
            expectedDroppedEntry.ExtraWorn = false;
            expectedDroppedEntry.ExtraWornLeft = false;
            if (droppedEntry.Count != -message.Item.Count || !droppedEntry.CanBeMerged(expectedDroppedEntry) || droppedObject.Id != message.Item.BaseId || !droppedObject.CellId ||
                GridCellCoords::CalculateGridCellCoords(droppedObject.Position) != droppedObject.CurrentCoords)
            {
                rejectDroppedItemDrop();
                return;
            }

            CellIdComponent droppedCell{droppedObject.CellId, droppedObject.WorldSpaceId, droppedObject.CurrentCoords};
            const CellIdComponent senderCell = message.HasDroppedObject && message.HasSenderCell
                                                   ? CellIdComponent{message.SenderCellId, message.SenderWorldSpaceId, message.SenderCurrentCoords}
                                                   : acMessage.pPlayer->GetCellComponent();
            if (!droppedCell.IsInRange(senderCell, false))
            {
                rejectDroppedItemDrop();
                return;
            }
        }
    }

    Inventory::Entry inventoryChange = message.Item;
    if (message.Drop)
    {
        auto storedEntry = std::find_if(
            inventoryComponent.Content.Entries.begin(), inventoryComponent.Content.Entries.end(),
            [&message](const Inventory::Entry& acEntry) { return acEntry.CanBeMerged(message.Item); });

        if (storedEntry == inventoryComponent.Content.Entries.end())
        {
            storedEntry = std::find_if(
                inventoryComponent.Content.Entries.begin(), inventoryComponent.Content.Entries.end(),
                [&message](const Inventory::Entry& acEntry) { return acEntry.BaseId == message.Item.BaseId && acEntry.IsExtraDataEqualsIgnoringWorn(message.Item); });
        }

        if (storedEntry == inventoryComponent.Content.Entries.end() || storedEntry->Count < -message.Item.Count)
        {
            rejectDroppedItemDrop();
            return;
        }

        inventoryChange.ExtraWorn = storedEntry->ExtraWorn;
        inventoryChange.ExtraWornLeft = storedEntry->ExtraWornLeft;
    }

    inventoryComponent.Content.AddOrRemoveEntry(inventoryChange);

    if (dropId != 0)
    {
        auto& droppedData = droppedView.get<DroppedItemComponent>(*droppedIt).Data;
        auto& droppedEntry = droppedData.CurrentInventory.Entries.front();
        droppedEntry.Count -= message.Item.Count;
    }

    entt::entity createdDropEntity = entt::null;
    ObjectData createdDropData{};
    if (shouldCreateDroppedItem)
    {
        createdDropData = std::move(droppedObject);
        do
        {
            createdDropData.ServerId = m_nextDroppedItemId++;
            if (m_nextDroppedItemId == 0)
                m_nextDroppedItemId = 1;
        } while (createdDropData.ServerId == 0 || m_droppedItemEntities.contains(createdDropData.ServerId));
        createdDropData.IsDroppedItem = true;

        createdDropEntity = m_world.create();
        m_world.emplace<DroppedItemComponent>(createdDropEntity, createdDropData, acMessage.pPlayer->GetId());
        m_world.emplace<CellIdComponent>(createdDropEntity, createdDropData.CellId, createdDropData.WorldSpaceId, createdDropData.CurrentCoords);
        m_droppedItemEntities.emplace(createdDropData.ServerId, createdDropEntity);
        if (createdDropData.LocalReferenceId != 0)
            m_droppedItemsByLocalReference.emplace(GetDroppedItemReferenceKey(acMessage.pPlayer->GetId(), createdDropData.LocalReferenceId), createdDropEntity);
        m_droppedItemsByCell[DroppedItemCellKey{createdDropData.CellId, createdDropData.WorldSpaceId, createdDropData.CurrentCoords}].insert(createdDropEntity);
    }

    const bool shouldNotifyDroppedItem = dropId != 0 || shouldCreateDroppedItem;
    if (!message.UpdateClients && !isRemoteNpcInteraction && !shouldNotifyDroppedItem)
        return;

    NotifyInventoryChanges notify;
    notify.ServerId = message.ServerId;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Item = inventoryChange;

    notify.DropId = dropId;
    notify.HasDroppedObject = shouldNotifyDroppedItem;
    if (shouldCreateDroppedItem)
        notify.DroppedObject = createdDropData;
    else if (dropId != 0 && droppedIt != droppedView.end())
    {
        auto& droppedData = droppedView.get<DroppedItemComponent>(*droppedIt).Data;
        notify.DroppedObject = droppedData;
        if (droppedData.CurrentInventory.Entries.front().Count <= 0)
            notify.DroppedObject.CurrentInventory.Entries.front().Count = 0;
    }

    notify.Drop = message.Drop;

    if (shouldCreateDroppedItem)
    {
        NotifyInventoryChanges peerNotify = notify;
        peerNotify.DroppedObject.LocalReferenceId = 0;
        if (!GameServer::Get()->SendToPlayersInRange(peerNotify, createdDropEntity, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

        NotifyInventoryChanges senderNotify = notify;
        senderNotify.Item.Count = 0;
        acMessage.pPlayer->Send(senderNotify);
        return;
    }

    if (dropId != 0 && droppedIt != droppedView.end())
    {
        const entt::entity dropEntity = *droppedIt;
        auto& droppedItem = droppedView.get<DroppedItemComponent>(dropEntity);
        NotifyInventoryChanges peerNotify = notify;
        peerNotify.DroppedObject.LocalReferenceId = 0;
        if (!GameServer::Get()->SendToPlayersInRange(peerNotify, dropEntity, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

        NotifyInventoryChanges senderNotify = notify;
        senderNotify.Item.Count = 0;
        senderNotify.DroppedObject.LocalReferenceId = 0;
        if (droppedItem.OriginPlayerId == acMessage.pPlayer->GetId() && droppedItem.Data.LocalReferenceId == message.ReferenceFormId)
            senderNotify.DroppedObject.LocalReferenceId = droppedItem.Data.LocalReferenceId;
        acMessage.pPlayer->Send(senderNotify);

        if (droppedItem.Data.CurrentInventory.Entries.front().Count <= 0)
        {
            const DroppedItemCellKey cellKey{droppedItem.Data.CellId, droppedItem.Data.WorldSpaceId, droppedItem.Data.CurrentCoords};
            const auto cellIt = m_droppedItemsByCell.find(cellKey);
            if (cellIt != m_droppedItemsByCell.end())
            {
                cellIt->second.erase(dropEntity);
                if (cellIt->second.empty())
                    m_droppedItemsByCell.erase(cellIt);
            }

            m_droppedItemEntities.erase(dropId);
            if (droppedItem.Data.LocalReferenceId != 0)
                m_droppedItemsByLocalReference.erase(GetDroppedItemReferenceKey(droppedItem.OriginPlayerId, droppedItem.Data.LocalReferenceId));
            m_world.destroy(dropEntity);
        }
        return;
    }

    const entt::entity cOrigin = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cOrigin, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnEquipmentChanges(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(static_cast<entt::entity>(message.ServerId));

    if (it == view.end())
        return;

    const auto* pOwnerComponent = m_world.try_get<OwnerComponent>(*it);
    if (pOwnerComponent)
    {
        if (pOwnerComponent->GetOwner() != acMessage.pPlayer || pOwnerComponent->OwnershipEpoch != message.OwnershipEpoch)
        {
            const uint32_t ownerId = pOwnerComponent->GetOwner() ? pOwnerComponent->GetOwner()->GetId() : 0;
            spdlog::debug(
                "Rejected equipment change from player {:X} for actor {:X}; current owner is {:X} and requested epoch {} does not match {}",
                acMessage.pPlayer->GetId(), message.ServerId, ownerId, message.OwnershipEpoch, pOwnerComponent->OwnershipEpoch);
            return;
        }
    }
    else if (message.OwnershipEpoch != 0)
    {
        spdlog::warn(
            "Rejected equipment change from player {:X} because object {:X} unexpectedly carried ownership epoch {}",
            acMessage.pPlayer->GetId(), message.ServerId, message.OwnershipEpoch);
        return;
    }

    auto& inventoryComponent = view.get<InventoryComponent>(*it);
    inventoryComponent.Content.UpdateEquipment(message.CurrentInventory);

    NotifyEquipmentChanges notify;
    notify.ServerId = message.ServerId;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.ItemId = message.ItemId;
    notify.EquipSlotId = message.EquipSlotId;
    notify.Count = message.Count;
    notify.Unequip = message.Unequip;
    notify.IsSpell = message.IsSpell;
    notify.IsShout = message.IsShout;
    notify.CurrentInventory = inventoryComponent.Content;

    const entt::entity cOrigin = static_cast<entt::entity>(message.ServerId);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cOrigin, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it != std::end(characterView) && characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
    {
        auto& characterComponent = characterView.get<CharacterComponent>(*it);
        if (characterComponent.IsWeaponDrawn() == message.IsWeaponDrawn)
            return;

        characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
        spdlog::debug("Updating weapon drawn state {:x}:{}", message.Id, message.IsWeaponDrawn);

        NotifyDrawWeapon notify{};
        notify.Id = message.Id;
        notify.OwnershipEpoch = message.OwnershipEpoch;
        notify.IsWeaponDrawn = message.IsWeaponDrawn;
        if (!GameServer::Get()->SendToPlayersInRange(notify, *it, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
}
