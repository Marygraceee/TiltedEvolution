#include <Services/CombatService.h>
#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <cmath>

CombatService::CombatService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_projectileLaunchConnection = aDispatcher.sink<PacketEvent<ProjectileLaunchRequest>>().connect<&CombatService::OnProjectileLaunchRequest>(this);
}

void CombatService::OnProjectileLaunchRequest(const PacketEvent<ProjectileLaunchRequest>& acMessage) const noexcept
{
    const auto& packet = acMessage.Packet;
    if (packet.CastingSource < 0 || packet.CastingSource >= 4 || !std::isfinite(packet.OriginX) || !std::isfinite(packet.OriginY) || !std::isfinite(packet.OriginZ) ||
        !std::isfinite(packet.ZAngle) || !std::isfinite(packet.XAngle) || !std::isfinite(packet.YAngle) || !std::isfinite(packet.Power) || !std::isfinite(packet.Scale))
        return;

    auto view = m_world.view<OwnerComponent>();
    const auto shooterIt = view.find(static_cast<entt::entity>(packet.ShooterID));
    if (shooterIt == view.end() || !view.get<OwnerComponent>(*shooterIt).IsCurrentOwner(acMessage.pPlayer, packet.OwnershipEpoch))
        return;

    NotifyProjectileLaunch notify{};

    notify.ShooterID = packet.ShooterID;
    notify.OwnershipEpoch = packet.OwnershipEpoch;

    notify.OriginX = packet.OriginX;
    notify.OriginY = packet.OriginY;
    notify.OriginZ = packet.OriginZ;

    notify.ProjectileBaseID = packet.ProjectileBaseID;
    notify.WeaponID = packet.WeaponID;
    notify.AmmoID = packet.AmmoID;

    notify.ZAngle = packet.ZAngle;
    notify.XAngle = packet.XAngle;
    notify.YAngle = packet.YAngle;

    notify.ParentCellID = packet.ParentCellID;

    notify.SpellID = packet.SpellID;
    notify.CastingSource = packet.CastingSource;

    notify.Area = packet.Area;
    notify.Power = packet.Power;
    notify.Scale = packet.Scale;

    notify.AlwaysHit = packet.AlwaysHit;
    notify.NoDamageOutsideCombat = packet.NoDamageOutsideCombat;
    notify.AutoAim = packet.AutoAim;
    notify.DeferInitialization = packet.DeferInitialization;
    notify.ForceConeOfFire = packet.ForceConeOfFire;

    notify.UnkBool1 = packet.UnkBool1;
    notify.UnkBool2 = packet.UnkBool2;

    if (!GameServer::Get()->SendToPlayersInRange(notify, *shooterIt, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}
