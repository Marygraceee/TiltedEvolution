#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/GridCellCoords.h>

struct CellIdComponent
{
    enum class RangeTransition
    {
        None,
        Entered,
        Left
    };
    CellIdComponent() {}

    CellIdComponent(GameId aCellId)
        : Cell(aCellId)
    {
    }

    CellIdComponent(GameId aCellId, GameId aWorldSpaceId, GridCellCoords aCenterCoords)
        : Cell(aCellId)
        , WorldSpaceId(aWorldSpaceId)
        , CenterCoords(aCenterCoords)
    {
    }

    bool operator==(const CellIdComponent& acRhs) const noexcept { return Cell == acRhs.Cell; }

    bool operator!=(const CellIdComponent& acRhs) const noexcept { return !operator==(acRhs); }

    operator bool() const noexcept { return *this != CellIdComponent{}; }

    bool IsInInteriorCell() const noexcept { return !WorldSpaceId; }

    bool IsInRange(const CellIdComponent& acRhs, bool aIsDragon) const noexcept
    {
        if (IsInInteriorCell())
            return Cell == acRhs.Cell;

        if (WorldSpaceId != acRhs.WorldSpaceId)
            return false;

        return GridCellCoords::IsCellInGridCell(acRhs.CenterCoords, CenterCoords, aIsDragon);
    }

    static RangeTransition GetRangeTransition(const CellIdComponent& acPrevious, const CellIdComponent& acCurrent, const CellIdComponent& acOther, const bool aIsDragon) noexcept
    {
        const bool wasInRange = (acPrevious.WorldSpaceId || acPrevious.Cell) && acPrevious.IsInRange(acOther, aIsDragon);
        const bool isInRange = acCurrent.IsInRange(acOther, aIsDragon);
        if (wasInRange == isInRange)
            return RangeTransition::None;

        return isInRange ? RangeTransition::Entered : RangeTransition::Left;
    }

    GameId Cell{};
    GameId WorldSpaceId{};
    GridCellCoords CenterCoords{};
};
