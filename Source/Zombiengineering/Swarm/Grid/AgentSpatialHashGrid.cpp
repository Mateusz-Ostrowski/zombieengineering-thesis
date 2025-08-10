#include "AgentSpatialHashGrid.h"
#include "HAL/PlatformTime.h"

FAgentSpatialHashGrid::FAgentSpatialHashGrid(float InCellSize)
	: CellSize(InCellSize)
	, InvCellSize(1.f / InCellSize)
{
}

void FAgentSpatialHashGrid::Reset()
{
	for (auto& Pair : Grid)
	{
		Pair._Value.EntityData.Reset();
	}
}

void FAgentSpatialHashGrid::InsertEntity(const FMassEntityHandle& Entity, const FVector& Location)
{
	const int64 CellKey = HashCoord(GetCellCoord2D(Location));
	FGridCell& Cell = FindOrAddCell(CellKey);
	Cell.EntityData.Emplace(Entity, Location);
}

void FAgentSpatialHashGrid::QueryNearby(const FVector& Location, float Radius,
                                        TArray<FEntityData, TInlineAllocator<16>>& OutEntities,
                                        int32 MaxResults) const
{
	QueryNearby(Location, Radius, TNumericLimits<float>::Max(), OutEntities, MaxResults);
}

void FAgentSpatialHashGrid::QueryNearby(const FVector& Location, float Radius, float ZHalfHeight,
                                        TArray<FEntityData, TInlineAllocator<16>>& OutEntities,
                                        int32 MaxResults) const
{
	int32 Written = 0;
	const int32 CapReserve = (MaxResults > 0) ? FMath::Min(MaxResults, 16) : 16;
	OutEntities.Reserve(OutEntities.Num() + CapReserve);

	VisitNearby(Location, Radius, ZHalfHeight, MaxResults, [&](const FEntityData& E)
	{
		OutEntities.Emplace(E);
		++Written;
		return (MaxResults <= 0) || (Written < MaxResults);
	});
}

int32 FAgentSpatialHashGrid::EstimateCountAt(const FVector& Location, float Radius, float ZHalfHeight) const
{
	if (Grid.IsEmpty()) return 0;

	int32 Count = 0;
	VisitNearby(Location, Radius, ZHalfHeight, -1, [&](const FEntityData&)
	{
		++Count;
		return true;
	});
	return Count;
}

const FAgentSpatialHashGrid::FGridCell* FAgentSpatialHashGrid::FindCell(int64 Key) const
{
	if (const FKV* Pair = Grid.Find(Key))
	{
		return &Pair->_Value;
	}
	return nullptr;
}

FAgentSpatialHashGrid::FGridCell& FAgentSpatialHashGrid::FindOrAddCell(int64 Key)
{
	if (FKV* Existing = Grid.Find(Key))
	{
		return Existing->_Value;
	}
	FKV NewPair;
	NewPair._Key   = Key;
	NewPair._Value = FGridCell();
	FKV& Inserted = Grid.FindOrInsert(MoveTemp(NewPair));
	return Inserted._Value;
}
