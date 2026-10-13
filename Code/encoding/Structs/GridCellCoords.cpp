#include <Structs/GridCellCoords.h>
#include <TiltedCore/Serialization.hpp>

#include <cstdint>

using TiltedPhoques::Serialization;

GridCellCoords::GridCellCoords()
{
    Reset();
}

GridCellCoords::GridCellCoords(int32_t aX, int32_t aY) noexcept
    : X(aX)
    , Y(aY)
{
}

bool GridCellCoords::operator==(const GridCellCoords& acRhs) const noexcept
{
    return X == acRhs.X && Y == acRhs.Y;
}

bool GridCellCoords::operator!=(const GridCellCoords& acRhs) const noexcept
{
    return !this->operator==(acRhs);
}

void GridCellCoords::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, X);
    Serialization::WriteVarInt(aWriter, Y);
}

void GridCellCoords::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    X = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Y = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
}

GridCellCoords GridCellCoords::CalculateGridCellCoords(const Vector3_NetQuantize& aCoords) noexcept
{
    return CalculateGridCellCoords(aCoords.x, aCoords.y);
}

GridCellCoords GridCellCoords::CalculateGridCellCoords(const float aX, const float aY) noexcept
{
    auto x = static_cast<int32_t>(floor(aX / 4096.f));
    auto y = static_cast<int32_t>(floor(aY / 4096.f));
    return GridCellCoords(x, y);
}

bool GridCellCoords::AreGridCellsOverlapping(const GridCellCoords& aCoords1, const GridCellCoords& aCoords2) noexcept
{
    const int64_t distanceX = static_cast<int64_t>(aCoords1.X) - aCoords2.X;
    const int64_t distanceY = static_cast<int64_t>(aCoords1.Y) - aCoords2.Y;
    if (distanceX > -m_gridsToLoad && distanceX < m_gridsToLoad && distanceY > -m_gridsToLoad && distanceY < m_gridsToLoad)
        return true;
    return false;
}

bool GridCellCoords::IsCellInGridCell(const GridCellCoords& aCell, const GridCellCoords& aGridCell, bool aIsDragon) noexcept
{
    const int64_t distanceToBorder = (aIsDragon ? m_gridsToLoadIfDragon : m_gridsToLoad) / 2;
    const int64_t distanceX = static_cast<int64_t>(aCell.X) - aGridCell.X;
    const int64_t distanceY = static_cast<int64_t>(aCell.Y) - aGridCell.Y;
    return distanceX >= -distanceToBorder && distanceX <= distanceToBorder && distanceY >= -distanceToBorder && distanceY <= distanceToBorder;
}
