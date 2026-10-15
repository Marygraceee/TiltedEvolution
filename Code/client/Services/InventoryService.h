#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <Structs/ObjectData.h>

struct World;
struct TransportService;

struct AssignObjectsResponse;
struct DisconnectedEvent;
struct UpdateEvent;
struct NotifyObjectInventoryChanges;
struct NotifyInventoryChanges;
struct InventoryChangeEvent;
struct EquipmentChangeEvent;
struct NotifyEquipmentChanges;

/**
 * @brief Manages inventories of actors and containers.
 *
 * The initial contents of actor inventories are synced through spawn messages through CharacterService
 * @see CharacterService
 * The initial contents of container inventories are synced through the ObjectService
 * @see ObjectService
 * The InventoryService manages any changes to both the current equipment of actors,
 * and the contents of both actor and object inventories.
 */
struct InventoryService
{
    InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~InventoryService() noexcept = default;

    TP_NOCOPYMOVE(InventoryService);

    /**
     * @brief Check weapon state.
     * @see RunWeaponStateUpdates
     */
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;
    /**
     * @brief Sends out inventory changes made by the local client.
     */
    void OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept;
    /**
     * @brief Sends out equipment changes made by the local client.
     */
    void OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept;

    /**
     * @brief Applies inventory changes sent by the server.
     */
    void OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept;
    void OnAssignObjectsResponse(const AssignObjectsResponse& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    /**
     * @brief Applies equipment changes sent by the server.
     */
    void OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept;

private:
    struct PendingLocalDropPickup
    {
        uint32_t ServerId{};
        uint32_t OwnershipEpoch{};
        uint32_t DropId{};
        Inventory::Entry Item{};
        bool UpdateClients = true;
        bool Sent{};
    };

    /**
     * Checks whether local actors their weapon draw states have changed,
     * and if so, send the new states to the server.
     */
    void RunWeaponStateUpdates() noexcept;
    void ApplyDroppedItem(const ObjectData& acData) noexcept;
    uint32_t SpawnDroppedItem(const ObjectData& acData) noexcept;
    void TrackDroppedItem(const ObjectData& acData, const uint32_t aFormId, const bool aIsLocalReference) noexcept;
    void RemoveDroppedItem(const uint32_t aDropId, const bool aDeleteReference) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_inventoryConnection;
    entt::scoped_connection m_equipmentConnection;
    entt::scoped_connection m_inventoryChangeConnection;
    entt::scoped_connection m_equipmentChangeConnection;
    entt::scoped_connection m_assignObjectsConnection;
    entt::scoped_connection m_disconnectedConnection;

    std::unordered_map<uint32_t, uint32_t> m_droppedFormToServerId;
    std::unordered_map<uint32_t, uint32_t> m_droppedServerIdToForm;
    std::unordered_map<uint32_t, ObjectData> m_droppedServerData;
    std::unordered_map<uint32_t, ObjectData> m_pendingDroppedItems;
    std::unordered_set<uint32_t> m_localDroppedForms;
    std::unordered_set<uint32_t> m_pendingLocalDropForms;
    std::unordered_map<uint32_t, PendingLocalDropPickup> m_pendingLocalDropPickups;
    double m_pendingDroppedItemRetryTimer{};
};
