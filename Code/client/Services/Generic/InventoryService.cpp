#include <Services/InventoryService.h>

#include <Messages/RequestObjectInventoryChanges.h>
#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/AssignObjectsResponse.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/DisconnectedEvent.h>

#include <World.h>
#include <Games/Skyrim/Interface/UI.h>
#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Actor.h>
#include <Structs/ObjectData.h>
#include <Forms/TESWorldSpace.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <EquipManager.h>
#include <Games/ActorExtension.h>
#include <Forms/TESNPC.h>
#include <DefaultObjectManager.h>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&InventoryService::OnUpdate>(this);
    m_inventoryConnection = m_dispatcher.sink<InventoryChangeEvent>().connect<&InventoryService::OnInventoryChangeEvent>(this);
    m_equipmentConnection = m_dispatcher.sink<EquipmentChangeEvent>().connect<&InventoryService::OnEquipmentChangeEvent>(this);
    m_inventoryChangeConnection = m_dispatcher.sink<NotifyInventoryChanges>().connect<&InventoryService::OnNotifyInventoryChanges>(this);
    m_equipmentChangeConnection = m_dispatcher.sink<NotifyEquipmentChanges>().connect<&InventoryService::OnNotifyEquipmentChanges>(this);
    m_assignObjectsConnection = m_dispatcher.sink<AssignObjectsResponse>().connect<&InventoryService::OnAssignObjectsResponse>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&InventoryService::OnDisconnected>(this);
}

void InventoryService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    RunWeaponStateUpdates();

    if (m_pendingDroppedItems.empty())
    {
        m_pendingDroppedItemRetryTimer = 0.0;
        return;
    }

    m_pendingDroppedItemRetryTimer += acUpdateEvent.Delta;
    if (m_pendingDroppedItemRetryTimer < 0.25)
        return;

    m_pendingDroppedItemRetryTimer = 0.0;
    Vector<uint32_t> pendingDropIds;
    pendingDropIds.reserve(m_pendingDroppedItems.size());
    for (const auto& pendingDrop : m_pendingDroppedItems)
        pendingDropIds.push_back(pendingDrop.first);

    for (const uint32_t dropId : pendingDropIds)
    {
        const auto pendingIt = m_pendingDroppedItems.find(dropId);
        if (pendingIt == m_pendingDroppedItems.end())
            continue;

        const ObjectData pendingData = pendingIt->second;
        ApplyDroppedItem(pendingData);
    }
}

void InventoryService::OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.FormId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    uint32_t serverId = 0;
    if (acEvent.OwnershipEpoch != 0)
    {
        const auto* pLocalComponent = m_world.try_get<LocalComponent>(*iter);
        const auto* pRemoteComponent = m_world.try_get<RemoteComponent>(*iter);
        const bool ownershipMatches = (pLocalComponent && pLocalComponent->Id == acEvent.ServerId && pLocalComponent->OwnershipEpoch == acEvent.OwnershipEpoch)
            || (pRemoteComponent && pRemoteComponent->Id == acEvent.ServerId && pRemoteComponent->OwnershipEpoch == acEvent.OwnershipEpoch);

        if (!ownershipMatches)
        {
            spdlog::debug("Discarded an inventory change for actor {:X} because ownership changed after it was queued (epoch {})", acEvent.ServerId, acEvent.OwnershipEpoch);
            return;
        }

        serverId = acEvent.ServerId;
    }
    else
    {
        if (Cast<Actor>(TESForm::GetById(acEvent.FormId)))
            return;

        const std::optional<uint32_t> serverIdRes = Utils::GetServerId(*iter);
        if (!serverIdRes)
        {
            spdlog::warn(
                "Discarded inventory change for form {:X} because it has no server entity (item {:X}, count {})", acEvent.FormId, acEvent.Item.BaseId.BaseId, acEvent.Item.Count);
            return;
        }
        serverId = *serverIdRes;
    }

    RequestInventoryChanges request;
    request.ServerId = serverId;
    request.OwnershipEpoch = acEvent.OwnershipEpoch;
    request.Item = acEvent.Item;
    request.Drop = acEvent.Drop;
    request.UpdateClients = acEvent.UpdateClients;
    request.ReferenceFormId = acEvent.ReferenceFormId;
    bool addedPendingLocalDrop = false;
    bool waitingForLocalDropPickup = false;

    if (acEvent.ReferenceFormId != 0)
    {
        const auto droppedIt = m_droppedFormToServerId.find(acEvent.ReferenceFormId);
        if (droppedIt != m_droppedFormToServerId.end())
        {
            request.DropId = droppedIt->second;
        }
        else if (!acEvent.Drop && m_pendingLocalDropForms.contains(acEvent.ReferenceFormId))
        {
            PendingLocalDropPickup pickup{};
            pickup.ServerId = serverId;
            pickup.OwnershipEpoch = acEvent.OwnershipEpoch;
            pickup.Item = acEvent.Item;
            pickup.UpdateClients = acEvent.UpdateClients;
            m_pendingLocalDropPickups.try_emplace(acEvent.ReferenceFormId, pickup);
            waitingForLocalDropPickup = true;
        }
        else if (acEvent.Drop)
        {
            addedPendingLocalDrop = m_pendingLocalDropForms.insert(acEvent.ReferenceFormId).second;
            TESObjectREFR* pDroppedObject = Cast<TESObjectREFR>(TESForm::GetById(acEvent.ReferenceFormId));
            TESObjectCELL* pCell = pDroppedObject ? pDroppedObject->GetParentCellEx() : nullptr;
            if (pDroppedObject && pCell && pDroppedObject->baseForm && m_world.GetModSystem().GetServerModId(pDroppedObject->baseForm->formID, request.DroppedObject.Id) &&
                m_world.GetModSystem().GetServerModId(pCell->formID, request.DroppedObject.CellId))
            {
                bool worldSpaceIsKnown = true;
                if (TESWorldSpace* pWorldSpace = pDroppedObject->GetWorldSpace())
                {
                    if (!m_world.GetModSystem().GetServerModId(pWorldSpace->formID, request.DroppedObject.WorldSpaceId))
                        worldSpaceIsKnown = false;
                }

                if (worldSpaceIsKnown)
                {
                    request.DroppedObject.CurrentCoords = GridCellCoords::CalculateGridCellCoords(pDroppedObject->position.x, pDroppedObject->position.y);
                    request.DroppedObject.Position = pDroppedObject->position;
                    request.DroppedObject.Rotation.x = pDroppedObject->rotation.x;
                    request.DroppedObject.Rotation.y = pDroppedObject->rotation.z;
                    request.DroppedObject.LocalReferenceId = pDroppedObject->formID;
                    request.DroppedObject.IsDroppedItem = true;
                    Inventory::Entry droppedEntry = acEvent.Item;
                    droppedEntry.Count = -droppedEntry.Count;
                    if (ExtraDataList* pExtraData = pDroppedObject->GetExtraDataList())
                        TESObjectREFR::GetItemFromExtraData(droppedEntry, pExtraData);
                    droppedEntry.Count = -acEvent.Item.Count;
                    droppedEntry.ExtraWorn = false;
                    droppedEntry.ExtraWornLeft = false;
                    request.DroppedObject.CurrentInventory.Entries.push_back(std::move(droppedEntry));
                    request.HasDroppedObject = true;
                }
            }
        }
    }

    if (request.HasDroppedObject)
    {
        PlayerCharacter* pPlayer = PlayerCharacter::Get();
        TESObjectCELL* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
        if (pPlayer && pCell && m_world.GetModSystem().GetServerModId(pCell->formID, request.SenderCellId))
        {
            request.SenderCurrentCoords = GridCellCoords::CalculateGridCellCoords(pPlayer->position.x, pPlayer->position.y);
            if (TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace())
            {
                if (m_world.GetModSystem().GetServerModId(pWorldSpace->formID, request.SenderWorldSpaceId))
                    request.HasSenderCell = true;
            }
            else
                request.HasSenderCell = true;
        }
    }

    if (waitingForLocalDropPickup)
        return;

    if (!m_transport.Send(request))
    {
        if (addedPendingLocalDrop)
            m_pendingLocalDropForms.erase(acEvent.ReferenceFormId);
    }

    spdlog::info("Sending item request, item: {:X}, count: {}, target object: {:X}", acEvent.Item.BaseId.BaseId, acEvent.Item.Count, acEvent.FormId);
}

void InventoryService::OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.ActorId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    const auto* pLocalComponent = m_world.try_get<LocalComponent>(*iter);
    if (acEvent.OwnershipEpoch == 0 || !pLocalComponent || pLocalComponent->Id != acEvent.ServerId || pLocalComponent->OwnershipEpoch != acEvent.OwnershipEpoch)
    {
        spdlog::debug("Discarded an equipment change for actor {:X} because ownership changed after it was queued (epoch {})", acEvent.ServerId, acEvent.OwnershipEpoch);
        return;
    }

    Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.ActorId));
    if (!pActor)
        return;

    auto& modSystem = World::Get().GetModSystem();

    RequestEquipmentChanges request;
    request.ServerId = acEvent.ServerId;
    request.OwnershipEpoch = acEvent.OwnershipEpoch;

    if (!modSystem.GetServerModId(acEvent.EquipSlotId, request.EquipSlotId))
        return;
    if (!modSystem.GetServerModId(acEvent.ItemId, request.ItemId))
        return;

    request.Count = acEvent.Count;
    request.Unequip = acEvent.Unequip;
    request.IsSpell = acEvent.IsSpell;
    request.IsShout = acEvent.IsShout;
    request.IsAmmo = acEvent.IsAmmo;
    request.CurrentInventory = pActor->GetEquipment();

    m_transport.Send(request);

    spdlog::info("Sending equipment request, item: {:X}, count: {}, target object: {:X}", acEvent.ItemId, acEvent.Count, acEvent.ActorId);
}

void InventoryService::OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept
{
    Actor* pActor = nullptr;
    TESObjectREFR* pReference = nullptr;
    entt::entity actorEntity = entt::null;
    if (acMessage.OwnershipEpoch != 0)
    {
        auto remoteView = m_world.view<RemoteComponent>(entt::exclude<LocalComponent>);
        const auto remoteIt = std::find_if(remoteView.begin(), remoteView.end(), [remoteView, &acMessage](const entt::entity aEntity)
        {
            const auto& remoteComponent = remoteView.get<RemoteComponent>(aEntity);
            return remoteComponent.Id == acMessage.ServerId && remoteComponent.OwnershipEpoch == acMessage.OwnershipEpoch;
        });

        if (remoteIt != remoteView.end())
        {
            actorEntity = *remoteIt;
            const auto& remoteComponent = remoteView.get<RemoteComponent>(*remoteIt);
            const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*remoteIt);
            const uint32_t formId = pFormIdComponent ? pFormIdComponent->Id : remoteComponent.CachedRefId;
            pActor = Cast<Actor>(TESForm::GetById(formId));
        }
        else
        {
            auto localView = m_world.view<LocalComponent, FormIdComponent>();
            const auto localIt = std::find_if(localView.begin(), localView.end(), [localView, &acMessage](const entt::entity aEntity)
            {
                const auto& localComponent = localView.get<LocalComponent>(aEntity);
                return localComponent.Id == acMessage.ServerId && localComponent.OwnershipEpoch == acMessage.OwnershipEpoch;
            });

            if (localIt != localView.end())
            {
                actorEntity = *localIt;
                pActor = Cast<Actor>(TESForm::GetById(localView.get<FormIdComponent>(*localIt).Id));
            }
        }
    }
    else
    {
        pReference = Utils::GetByServerId<TESObjectREFR>(acMessage.ServerId);
        pActor = Cast<Actor>(pReference);
    }

    if (acMessage.DropRejected && acMessage.OwnershipEpoch != 0 && actorEntity == entt::null)
    {
        auto localView = m_world.view<LocalComponent, FormIdComponent>();
        const auto localIt = std::find_if(
            localView.begin(), localView.end(), [localView, &acMessage](const entt::entity aEntity) { return localView.get<LocalComponent>(aEntity).Id == acMessage.ServerId; });

        if (localIt != localView.end())
        {
            actorEntity = *localIt;
            pActor = Cast<Actor>(TESForm::GetById(localView.get<FormIdComponent>(*localIt).Id));
        }
        else
        {
            auto remoteView = m_world.view<RemoteComponent>();
            const auto remoteIt = std::find_if(
                remoteView.begin(), remoteView.end(),
                [remoteView, &acMessage](const entt::entity aEntity) { return remoteView.get<RemoteComponent>(aEntity).Id == acMessage.ServerId; });

            if (remoteIt != remoteView.end())
            {
                actorEntity = *remoteIt;
                const auto& remoteComponent = remoteView.get<RemoteComponent>(*remoteIt);
                const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*remoteIt);
                const uint32_t formId = pFormIdComponent ? pFormIdComponent->Id : remoteComponent.CachedRefId;
                pActor = Cast<Actor>(TESForm::GetById(formId));
            }
        }
    }

    const auto updatePendingInventory = [this, actorEntity, &acMessage]()
    {
        if (actorEntity == entt::null)
            return;

        auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(actorEntity);
        if (pWaitingFor3D)
            pWaitingFor3D->SpawnRequest.InventoryContent.AddOrRemoveEntry(acMessage.Item);
    };

    if (acMessage.DropRejected)
    {
        const bool pickedUpBeforeRejection = acMessage.ReferenceFormId != 0 && m_pendingLocalDropPickups.erase(acMessage.ReferenceFormId) != 0;
        const bool hadPendingDrop = acMessage.ReferenceFormId == 0 || m_pendingLocalDropForms.erase(acMessage.ReferenceFormId) != 0;

        if (hadPendingDrop && acMessage.ReferenceFormId != 0)
        {
            ScopedReferencesOverride referencesOverride;
            TESObjectREFR* pDroppedReference = Cast<TESObjectREFR>(TESForm::GetById(acMessage.ReferenceFormId));
            if (pDroppedReference && pDroppedReference->IsTemporary())
                pDroppedReference->Delete();
        }

        if (hadPendingDrop && !pickedUpBeforeRejection && acMessage.Item.Count > 0)
        {
            updatePendingInventory();
            if (pActor)
            {
                ScopedInventoryOverride inventoryOverride;
                pActor->AddOrRemoveItem(acMessage.Item);
            }
        }
        return;
    }

    if (acMessage.Drop)
    {
        if (acMessage.HasDroppedObject)
        {
            if (acMessage.Item.Count != 0)
                updatePendingInventory();

            if (pActor && acMessage.Item.Count != 0)
            {
                ScopedInventoryOverride _;
                pActor->AddOrRemoveItem(acMessage.Item);
            }

            ApplyDroppedItem(acMessage.DroppedObject);
        }
        else if (pActor && acMessage.Item.Count != 0)
        {
            updatePendingInventory();
            ScopedInventoryOverride _;
            pActor->DropOrPickUpObject(acMessage.Item, nullptr, nullptr);
        }
        else if (acMessage.Item.Count != 0)
            updatePendingInventory();
        return;
    }

    if (acMessage.DropId != 0)
    {
        if (acMessage.Item.Count != 0)
            updatePendingInventory();

        if (pActor && acMessage.Item.Count != 0)
        {
            ScopedInventoryOverride _;
            pActor->AddOrRemoveItem(acMessage.Item);
        }

        if (acMessage.HasDroppedObject && acMessage.DroppedObject.LocalReferenceId != 0)
        {
            const auto pendingPickup = m_pendingLocalDropPickups.find(acMessage.DroppedObject.LocalReferenceId);
            if (pendingPickup != m_pendingLocalDropPickups.end() && pendingPickup->second.DropId == acMessage.DropId)
            {
                m_pendingLocalDropPickups.erase(pendingPickup);
                m_pendingLocalDropForms.erase(acMessage.DroppedObject.LocalReferenceId);
            }
        }
        else if (acMessage.Item.Count < 0)
        {
            const auto pendingPickup = std::find_if(
                m_pendingLocalDropPickups.begin(), m_pendingLocalDropPickups.end(),
                [&acMessage](const auto& entry) { return entry.second.Sent && entry.second.DropId == acMessage.DropId; });
            if (pendingPickup != m_pendingLocalDropPickups.end())
            {
                m_pendingLocalDropForms.erase(pendingPickup->first);
                m_pendingLocalDropPickups.erase(pendingPickup);
            }
        }

        if (acMessage.HasDroppedObject)
            ApplyDroppedItem(acMessage.DroppedObject);
        else
            RemoveDroppedItem(acMessage.DropId, true);
        return;
    }

    if (acMessage.OwnershipEpoch != 0 && acMessage.Item.Count != 0)
        updatePendingInventory();

    if (!pActor)
    {
        if (pReference && acMessage.OwnershipEpoch == 0)
        {
            ScopedInventoryOverride _;
            pReference->AddOrRemoveItem(acMessage.Item);
            return;
        }

        if (acMessage.OwnershipEpoch != 0)
            spdlog::debug("Discarded an inventory update for actor {:X} because epoch {} is no longer current", acMessage.ServerId, acMessage.OwnershipEpoch);
        return;
    }

    ScopedInventoryOverride _;
    pActor->AddOrRemoveItem(acMessage.Item);
}

void InventoryService::OnAssignObjectsResponse(const AssignObjectsResponse& acMessage) noexcept
{
    std::unordered_set<uint32_t> assignedDropIds;
    for (const auto& object : acMessage.Objects)
    {
        if (!object.IsDroppedItem)
            continue;

        assignedDropIds.insert(object.ServerId);
        const auto existingIt = m_droppedServerIdToForm.find(object.ServerId);
        if (existingIt != m_droppedServerIdToForm.end())
        {
            TESObjectREFR* pReference = Cast<TESObjectREFR>(TESForm::GetById(existingIt->second));
            const auto existingDataIt = m_droppedServerData.find(object.ServerId);
            if (pReference && pReference->IsTemporary() && existingDataIt != m_droppedServerData.end() && existingDataIt->second == object)
                continue;
        }

        ApplyDroppedItem(object);
    }

    const auto isInRequestedArea = [&acMessage](const ObjectData& aData)
    {
        if (acMessage.WorldSpaceId)
            return aData.WorldSpaceId == acMessage.WorldSpaceId && GridCellCoords::IsCellInGridCell(aData.CurrentCoords, acMessage.CurrentCoords, false);

        return !aData.WorldSpaceId && aData.CellId == acMessage.CellId;
    };

    Vector<uint32_t> dropsToRemove;
    for (const auto& [dropId, droppedData] : m_droppedServerData)
    {
        if (assignedDropIds.contains(dropId) || !isInRequestedArea(droppedData))
            continue;

        dropsToRemove.push_back(dropId);
    }

    for (uint32_t dropId : dropsToRemove)
        RemoveDroppedItem(dropId, true);

    for (auto pendingIt = m_pendingDroppedItems.begin(); pendingIt != m_pendingDroppedItems.end();)
    {
        if (assignedDropIds.contains(pendingIt->first) || !isInRequestedArea(pendingIt->second))
        {
            ++pendingIt;
            continue;
        }

        const uint32_t localReferenceId = pendingIt->second.LocalReferenceId;
        if (localReferenceId != 0)
        {
            m_pendingLocalDropForms.erase(localReferenceId);
            if (!m_localDroppedForms.contains(localReferenceId))
            {
                TESObjectREFR* pReference = Cast<TESObjectREFR>(TESForm::GetById(localReferenceId));
                if (pReference && pReference->IsTemporary())
                {
                    ScopedReferencesOverride referencesOverride;
                    pReference->Delete();
                }
            }
        }

        pendingIt = m_pendingDroppedItems.erase(pendingIt);
    }
}

void InventoryService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    ScopedReferencesOverride referencesOverride;
    Vector<uint32_t> remoteDropIds;
    for (const auto& [dropId, formId] : m_droppedServerIdToForm)
    {
        if (m_localDroppedForms.contains(formId))
            continue;

        TESObjectREFR* pReference = Cast<TESObjectREFR>(TESForm::GetById(formId));
        if (pReference && pReference->IsTemporary())
            pReference->Delete();

        remoteDropIds.push_back(dropId);
    }

    for (uint32_t dropId : remoteDropIds)
        RemoveDroppedItem(dropId, false);

    for (uint32_t formId : m_pendingLocalDropForms)
    {
        if (m_localDroppedForms.contains(formId))
            continue;

        TESObjectREFR* pReference = Cast<TESObjectREFR>(TESForm::GetById(formId));
        if (pReference && pReference->IsTemporary())
            pReference->Delete();
    }

    m_pendingLocalDropForms.clear();
    m_pendingLocalDropPickups.clear();
    m_pendingDroppedItems.clear();
    m_pendingDroppedItemRetryTimer = 0.0;
}

void InventoryService::ApplyDroppedItem(const ObjectData& acData) noexcept
{
    if (!acData.IsDroppedItem || acData.ServerId == 0 || acData.CurrentInventory.Entries.size() != 1)
    {
        m_pendingDroppedItems.erase(acData.ServerId);
        return;
    }

    const auto& item = acData.CurrentInventory.Entries.front();
    if (acData.LocalReferenceId != 0)
    {
        const auto pendingPickup = m_pendingLocalDropPickups.find(acData.LocalReferenceId);
        if (pendingPickup != m_pendingLocalDropPickups.end())
        {
            m_pendingDroppedItems.erase(acData.ServerId);
            m_pendingLocalDropForms.erase(acData.LocalReferenceId);
            TrackDroppedItem(acData, acData.LocalReferenceId, true);

            if (!pendingPickup->second.Sent)
            {
                RequestInventoryChanges request;
                request.ServerId = pendingPickup->second.ServerId;
                request.OwnershipEpoch = pendingPickup->second.OwnershipEpoch;
                request.Item = pendingPickup->second.Item;
                request.DropId = acData.ServerId;
                request.ReferenceFormId = acData.LocalReferenceId;
                request.UpdateClients = pendingPickup->second.UpdateClients;
                pendingPickup->second.DropId = acData.ServerId;
                pendingPickup->second.Sent = true;
                if (!m_transport.Send(request))
                    pendingPickup->second.Sent = false;
            }
            return;
        }

        m_pendingLocalDropForms.erase(acData.LocalReferenceId);
    }

    const auto pendingPickup =
        std::find_if(m_pendingLocalDropPickups.begin(), m_pendingLocalDropPickups.end(), [&acData](const auto& entry) { return entry.second.DropId == acData.ServerId; });
    if (pendingPickup != m_pendingLocalDropPickups.end())
    {
        m_pendingDroppedItems.erase(acData.ServerId);
        return;
    }

    if (item.Count <= 0)
    {
        RemoveDroppedItem(acData.ServerId, true);
        return;
    }

    if (acData.LocalReferenceId != 0)
    {
        TESObjectREFR* pLocalReference = Cast<TESObjectREFR>(TESForm::GetById(acData.LocalReferenceId));
        const uint32_t expectedBaseId = m_world.GetModSystem().GetGameId(item.BaseId);
        if (pLocalReference && pLocalReference->IsTemporary() && pLocalReference->baseForm && pLocalReference->baseForm->formID == expectedBaseId)
        {
            RemoveDroppedItem(acData.ServerId, false);
            TrackDroppedItem(acData, pLocalReference->formID, true);
            return;
        }
    }

    const uint32_t formId = SpawnDroppedItem(acData);
    if (formId == 0)
        return;

    RemoveDroppedItem(acData.ServerId, true);
    TrackDroppedItem(acData, formId, false);
}

uint32_t InventoryService::SpawnDroppedItem(const ObjectData& acData) noexcept
{
    if (acData.CurrentInventory.Entries.size() != 1)
        return 0;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
    {
        m_pendingDroppedItems.insert_or_assign(acData.ServerId, acData);
        return 0;
    }

    ModSystem& modSystem = m_world.GetModSystem();
    TESObjectCELL* pCell = nullptr;
    if (acData.WorldSpaceId)
    {
        TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(modSystem.GetGameId(acData.WorldSpaceId)));
        if (pWorldSpace)
            pCell = ModManager::Get()->GetCellFromCoordinates(acData.CurrentCoords.X, acData.CurrentCoords.Y, pWorldSpace, true);
    }
    else
        pCell = Cast<TESObjectCELL>(TESForm::GetById(modSystem.GetGameId(acData.CellId)));

    if (!pCell)
    {
        m_pendingDroppedItems.insert_or_assign(acData.ServerId, acData);
        return 0;
    }

    const auto& item = acData.CurrentInventory.Entries.front();
    NiPoint3 position{glm::vec3(acData.Position.x, acData.Position.y, acData.Position.z)};
    NiPoint3 rotation{glm::vec3(acData.Rotation.x, 0.f, acData.Rotation.y)};
    const uint32_t formId = pPlayer->CreateDroppedObject(item, &position, &rotation);
    TESObjectREFR* pReference = Cast<TESObjectREFR>(TESForm::GetById(formId));
    if (!pReference)
        return 0;

    if (pReference->GetParentCellEx() != pCell)
        pReference->MoveTo(pCell, position);

    pReference = Cast<TESObjectREFR>(TESForm::GetById(formId));
    if (!pReference)
        return 0;

    pReference->SetRotation(rotation.x, rotation.y, rotation.z);
    return formId;
}

void InventoryService::TrackDroppedItem(const ObjectData& acData, const uint32_t aFormId, const bool aIsLocalReference) noexcept
{
    const uint32_t aDropId = acData.ServerId;
    RemoveDroppedItem(aDropId, false);
    m_localDroppedForms.erase(aFormId);
    const auto formIt = m_droppedFormToServerId.find(aFormId);
    if (formIt != m_droppedFormToServerId.end())
        RemoveDroppedItem(formIt->second, false);

    m_droppedFormToServerId[aFormId] = aDropId;
    m_droppedServerIdToForm[aDropId] = aFormId;
    m_droppedServerData[aDropId] = acData;
    if (aIsLocalReference)
        m_localDroppedForms.insert(aFormId);
}

void InventoryService::RemoveDroppedItem(const uint32_t aDropId, const bool aDeleteReference) noexcept
{
    m_pendingDroppedItems.erase(aDropId);

    const auto dropIt = m_droppedServerIdToForm.find(aDropId);
    if (dropIt == m_droppedServerIdToForm.end())
        return;

    const uint32_t formId = dropIt->second;
    m_droppedServerIdToForm.erase(dropIt);
    m_droppedServerData.erase(aDropId);
    m_droppedFormToServerId.erase(formId);
    m_localDroppedForms.erase(formId);

    if (!aDeleteReference)
        return;

    TESObjectREFR* pReference = Cast<TESObjectREFR>(TESForm::GetById(formId));
    if (pReference && pReference->IsTemporary())
        pReference->Delete();
}

void InventoryService::OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept
{
    auto view = m_world.view<RemoteComponent>(entt::exclude<LocalComponent>);
    const auto it = std::find_if(view.begin(), view.end(), [view, &acMessage](const entt::entity aEntity)
    {
        const auto& remoteComponent = view.get<RemoteComponent>(aEntity);
        return remoteComponent.Id == acMessage.ServerId && remoteComponent.OwnershipEpoch == acMessage.OwnershipEpoch;
    });
    if (it == view.end())
    {
        spdlog::debug("Discarded an equipment update for actor {:X} because epoch {} is no longer current", acMessage.ServerId, acMessage.OwnershipEpoch);
        return;
    }

    const auto& remoteComponent = view.get<RemoteComponent>(*it);
    const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(*it);
    const uint32_t formId = pFormIdComponent ? pFormIdComponent->Id : remoteComponent.CachedRefId;
    Actor* pActor = Cast<Actor>(TESForm::GetById(formId));
    if (auto* pWaitingFor3D = m_world.try_get<WaitingFor3D>(*it))
        pWaitingFor3D->SpawnRequest.InventoryContent = acMessage.CurrentInventory;

    if (!pActor)
        return;

    auto& modSystem = World::Get().GetModSystem();

    uint32_t itemId = modSystem.GetGameId(acMessage.ItemId);
    TESForm* pItem = TESForm::GetById(itemId);

    if (!pItem)
    {
        spdlog::error("Could not find inventory item {:X}:{:X}", acMessage.ItemId.ModId, acMessage.ItemId.BaseId);
        return;
    }

    uint32_t equipSlotId = modSystem.GetGameId(acMessage.EquipSlotId);
    TESForm* pEquipSlot = TESForm::GetById(equipSlotId);

    uint32_t slotId = 0;
    if (pEquipSlot == DefaultObjectManager::Get().rightEquipSlot)
        slotId = 1;

    auto* pEquipManager = EquipManager::Get();

    if (acMessage.IsSpell)
    {
        if (acMessage.Unequip)
            pEquipManager->UnEquipSpell(pActor, pItem, slotId);
        else
            pEquipManager->EquipSpell(pActor, pItem, slotId);

        return;
    }
    else if (acMessage.IsShout)
    {
        if (acMessage.Unequip)
            pEquipManager->UnEquipShout(pActor, pItem);
        else
            pEquipManager->EquipShout(pActor, pItem);

        return;
    }

    // TODO: ExtraData necessary? probably
    if (acMessage.Unequip)
    {
        pEquipManager->UnEquip(pActor, pItem, nullptr, acMessage.Count, pEquipSlot, false, true, false, false, nullptr);
    }
    else
    {
        // Unequip all armor first, since the game won't auto unequip armor
        Inventory wornArmor{};
        if (pItem->formType == FormType::Armor)
        {
            wornArmor = pActor->GetWornArmor();
            for (const auto& armor : wornArmor.Entries)
            {
                uint32_t armorId = modSystem.GetGameId(armor.BaseId);
                TESForm* pArmor = TESForm::GetById(armorId);
                if (pArmor)
                    pEquipManager->UnEquip(pActor, pArmor, nullptr, 1, pEquipSlot, false, true, false, false, nullptr);
            }
        }

        pEquipManager->Equip(pActor, pItem, nullptr, acMessage.Count, pEquipSlot, false, true, false, false);

        for (const auto& armor : wornArmor.Entries)
        {
            uint32_t armorId = modSystem.GetGameId(armor.BaseId);
            TESForm* pArmor = TESForm::GetById(armorId);
            if (pArmor)
                pEquipManager->Equip(pActor, pArmor, nullptr, 1, pEquipSlot, false, true, false, false);
        }
    }
}

void InventoryService::RunWeaponStateUpdates() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 500ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent, LocalComponent>();

    for (auto entity : view)
    {
        const auto& formIdComponent = view.get<FormIdComponent>(entity);
        Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        auto& localComponent = view.get<LocalComponent>(entity);

        bool isWeaponDrawn = pActor->actorState.IsWeaponDrawn();
        if (isWeaponDrawn != localComponent.IsWeaponDrawn)
        {
            localComponent.IsWeaponDrawn = isWeaponDrawn;

            DrawWeaponRequest request;
            request.Id = localComponent.Id;
            request.OwnershipEpoch = localComponent.OwnershipEpoch;
            request.IsWeaponDrawn = isWeaponDrawn;

            m_transport.Send(request);
        }
    }
}
