#include <Components.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestDeathStateChange.h>
#include <Services/ActorValueService.h>
#include <World.h>
#include <GameServer.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Structs/ActorValues.h>
#include <cmath>

ActorValueService::ActorValueService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_updateHealthConnection = aDispatcher.sink<PacketEvent<RequestActorValueChanges>>().connect<&ActorValueService::OnActorValueChanges>(this);
    m_updateMaxValueConnection = aDispatcher.sink<PacketEvent<RequestActorMaxValueChanges>>().connect<&ActorValueService::OnActorMaxValueChanges>(this);
    m_updateDeltaHealthConnection = aDispatcher.sink<PacketEvent<RequestHealthChangeBroadcast>>().connect<&ActorValueService::OnHealthChangeBroadcast>(this);
    m_deathStateConnection = aDispatcher.sink<PacketEvent<RequestDeathStateChange>>().connect<&ActorValueService::OnDeathStateChange>(this);
}

void ActorValueService::OnActorValueChanges(const PacketEvent<RequestActorValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end() || !actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    NotifyActorValueChanges notify;
    for (auto& [id, value] : message.Values)
    {
        if (!ActorValues::IsValidValue(id, value))
            continue;

        actorValuesComponent.CurrentActorValues.ActorValuesList[id] = value;
        notify.Values.insert({id, value});
    }

    if (notify.Values.empty())
        return;

    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Id = acMessage.Packet.Id;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnActorMaxValueChanges(const PacketEvent<RequestActorMaxValueChanges>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));

    if (it == actorValuesView.end() || !actorValuesView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    NotifyActorMaxValueChanges notify;
    for (auto& [id, value] : message.Values)
    {
        if (!ActorValues::IsValidValue(id, value))
            continue;

        actorValuesComponent.CurrentActorValues.ActorMaxValuesList[id] = value;
        notify.Values.insert({id, value});
    }

    if (notify.Values.empty())
        return;

    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Id = message.Id;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnHealthChangeBroadcast(const PacketEvent<RequestHealthChangeBroadcast>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    // TODO(cosideci): should server side health not be updated?
    if (!std::isfinite(message.DeltaHealth))
        return;

    auto actorValuesView = m_world.view<ActorValuesComponent, OwnerComponent, CharacterComponent, CellIdComponent>();

    auto it = actorValuesView.find(static_cast<entt::entity>(message.Id));
    if (it == actorValuesView.end())
        return;

    const auto& ownerComponent = actorValuesView.get<OwnerComponent>(*it);
    const auto& characterComponent = actorValuesView.get<CharacterComponent>(*it);
    const auto& cellComponent = actorValuesView.get<CellIdComponent>(*it);
    if (message.OwnershipEpoch == 0 || ownerComponent.OwnershipEpoch != message.OwnershipEpoch ||
        !acMessage.pPlayer->GetCellComponent().IsInRange(cellComponent, characterComponent.IsDragon()))
        return;

    auto& actorValuesComponent = actorValuesView.get<ActorValuesComponent>(*it);
    const auto currentHealth = actorValuesComponent.CurrentActorValues.ActorValuesList[24];
    const auto updatedHealth = currentHealth + message.DeltaHealth;
    if (!std::isfinite(updatedHealth))
        return;

    actorValuesComponent.CurrentActorValues.ActorValuesList[24] = updatedHealth;

    NotifyHealthChangeBroadcast notify;
    notify.Id = message.Id;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.DeltaHealth = message.DeltaHealth;

    if (!GameServer::Get()->SendToPlayersInRange(notify, *it, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void ActorValueService::OnDeathStateChange(const PacketEvent<RequestDeathStateChange>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();

    const auto it = characterView.find(static_cast<entt::entity>(message.Id));

    if (it == characterView.end() || !characterView.get<OwnerComponent>(*it).IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch))
        return;

    auto& characterComponent = characterView.get<CharacterComponent>(*it);
    characterComponent.SetDead(message.IsDead);
    spdlog::debug("Updating death state {:x}:{}", message.Id, message.IsDead);

    NotifyDeathStateChange notify;
    notify.OwnershipEpoch = message.OwnershipEpoch;
    notify.Id = message.Id;
    notify.IsDead = message.IsDead;

    const entt::entity cEntity = static_cast<entt::entity>(message.Id);
    if (!GameServer::Get()->SendToPlayersInRange(notify, cEntity, acMessage.pPlayer))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
