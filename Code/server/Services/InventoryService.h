#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <Structs/GameId.h>
#include <Structs/GridCellCoords.h>
#include <Events/PacketEvent.h>

struct World;
struct UpdateEvent;
struct AssignObjectsResponse;
struct CellIdComponent;
struct RequestObjectInventoryChanges;
struct RequestInventoryChanges;
struct RequestEquipmentChanges;
struct DrawWeaponRequest;
struct PlayerLeaveCellEvent;

/**
 * @brief Relays inventory/equipment changes and updates the server side state.
 */
class InventoryService
{
public:
    InventoryService(World& aWorld, entt::dispatcher& aDispatcher);

    /**
     * @brief Relays inventory changes to other clients and updates server side inventories.
     */
    void OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept;
    /**
     * @brief Relays equipment changes to other clients and updates server side equipment.
     */
    void OnEquipmentChanges(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept;
    /**
     * @brief Relays weapon draw changes to other clients and updates server side weapon draw state.
     */
    void OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept;
    void AppendDroppedItems(AssignObjectsResponse& aResponse, const CellIdComponent& acCell, uint32_t aPlayerId) const noexcept;

private:
    struct DroppedItemCellKey
    {
        GameId Cell{};
        GameId WorldSpace{};
        GridCellCoords Coords{};

        bool operator==(const DroppedItemCellKey& acRhs) const noexcept
        {
            if (WorldSpace != GameId{} || acRhs.WorldSpace != GameId{})
                return WorldSpace == acRhs.WorldSpace && Coords == acRhs.Coords;
            return Cell == acRhs.Cell;
        }
    };

    struct DroppedItemCellKeyHash
    {
        std::size_t operator()(const DroppedItemCellKey& acKey) const noexcept
        {
            std::size_t seed = std::hash<GameId>{}(acKey.WorldSpace ? acKey.WorldSpace : acKey.Cell);
            if (acKey.WorldSpace == GameId{})
                return seed;

            const auto combine = [&seed](const std::size_t aValue)
            {
                seed ^= aValue + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            };
            combine(std::hash<int32_t>{}(acKey.Coords.X));
            combine(std::hash<int32_t>{}(acKey.Coords.Y));
            return seed;
        }
    };

    static uint64_t GetDroppedItemReferenceKey(uint32_t aPlayerId, uint32_t aReferenceFormId) noexcept { return (static_cast<uint64_t>(aPlayerId) << 32) | aReferenceFormId; }

    World& m_world;
    uint32_t m_nextDroppedItemId{1};
    std::unordered_map<uint32_t, entt::entity> m_droppedItemEntities;
    std::unordered_map<uint64_t, entt::entity> m_droppedItemsByLocalReference;
    std::unordered_map<DroppedItemCellKey, std::unordered_set<entt::entity>, DroppedItemCellKeyHash> m_droppedItemsByCell;

    entt::scoped_connection m_inventoryChangeConnection;
    entt::scoped_connection m_equipmentChangeConnection;
    entt::scoped_connection m_drawWeaponConnection;
};
