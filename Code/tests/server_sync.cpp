#include <cstdint>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Stl.hpp>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <Server.hpp>

using namespace TiltedPhoques;

#include <Components.h>
#include <catch2/catch.hpp>

TEST_CASE("Remote character visibility follows player cell transitions", "[server.sync]")
{
    const GameId worldSpace{1, 1};
    const CellIdComponent firstCell{GameId{1, 2}, worldSpace, {0, 0}};
    const CellIdComponent nextCell{GameId{1, 3}, worldSpace, {10, 10}};
    const CellIdComponent exteriorActor{GameId{1, 4}, worldSpace, {0, 0}};
    const CellIdComponent exteriorOrigin{GameId{1, 8}, worldSpace, {0, 0}};

    REQUIRE(CellIdComponent::GetRangeTransition({}, firstCell, exteriorActor, false) == CellIdComponent::RangeTransition::Entered);
    REQUIRE(CellIdComponent::GetRangeTransition(firstCell, firstCell, exteriorActor, false) == CellIdComponent::RangeTransition::None);
    REQUIRE(CellIdComponent::GetRangeTransition(firstCell, nextCell, exteriorActor, false) == CellIdComponent::RangeTransition::Left);
    REQUIRE(static_cast<bool>(exteriorOrigin));
    REQUIRE(exteriorOrigin.IsInRange(firstCell, false));

    const CellIdComponent interiorCell{GameId{1, 5}, {}, {}};
    const CellIdComponent interiorActor{GameId{1, 5}};
    REQUIRE(CellIdComponent::GetRangeTransition(firstCell, interiorCell, interiorActor, false) == CellIdComponent::RangeTransition::Entered);
    REQUIRE(CellIdComponent::GetRangeTransition(interiorCell, firstCell, interiorActor, false) == CellIdComponent::RangeTransition::Left);

    const CellIdComponent dragonActor{GameId{1, 7}, worldSpace, {8, 8}};
    REQUIRE(CellIdComponent::GetRangeTransition({}, firstCell, dragonActor, true) == CellIdComponent::RangeTransition::Entered);
    REQUIRE(CellIdComponent::GetRangeTransition({}, firstCell, dragonActor, false) == CellIdComponent::RangeTransition::None);
}
