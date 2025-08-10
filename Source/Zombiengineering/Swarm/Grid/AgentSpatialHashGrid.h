#pragma once

#include "MassEntityHandle.h"
#include "Containers/Array.h"
#include "Math/IntPoint.h"
#include "Math/Vector.h"
#include "HAL/PlatformMath.h"
#include "HAL/PlatformMisc.h"
#include "Limits.h"

#include "HashTable/HashTable.h"

#ifndef HASHGRID_LIGHT_HASH
#define HASHGRID_LIGHT_HASH 0
#endif

struct FInt64HashTraits
{
	static uint32 GetKeyHash(const int64& Key)
	{
		uint64 k = static_cast<uint64>(Key);
		k ^= k >> 33;
		k *= 0xff51afd7ed558ccdULL;
		k ^= k >> 33;
		k *= 0xc4ceb9fe1a85ec53ULL;
		k ^= k >> 33;
		return static_cast<uint32>(k) ^ static_cast<uint32>(k >> 32);
	}
};

struct FUEHashAllocator
{
	void* Allocate(size_t Bytes) { return FMemory::Malloc(Bytes, alignof(uint64)); }
	void  Deallocate(void* Ptr)  { FMemory::Free(Ptr); }
};

struct FEntityData
{
	FMassEntityHandle Entity;
	FVector Location;

	FEntityData(const FMassEntityHandle& InEntity, const FVector& InLocation)
		: Entity(InEntity), Location(InLocation)
	{}
};

class FAgentSpatialHashGrid
{
public:
	explicit FAgentSpatialHashGrid(float InCellSize = 200.f);

	struct FGridCell
	{
		TArray<FEntityData> EntityData;
		FGridCell() { EntityData.Reserve(8); }
	};

	void Reset();

	void InsertEntity(const FMassEntityHandle& Entity, const FVector& Location);

	void QueryNearby(const FVector& Location, float Radius,
	                 TArray<FEntityData, TInlineAllocator<16>>& OutEntities,
	                 int32 MaxResults = -1) const;

	void QueryNearby(const FVector& Location, float Radius, float ZHalfHeight,
	                 TArray<FEntityData, TInlineAllocator<16>>& OutEntities,
	                 int32 MaxResults) const;

	template <typename FVisitor>
	void VisitNearby(const FVector& Location, float Radius, float ZHalfHeight, int32 MaxResults, FVisitor&& Visitor) const
	{
		const int32 R = FMath::CeilToInt(Radius / CellSize);
		if (R <= 0) return;

		struct FStencil { int32 R = 0; TArray<FIntPoint> Offsets; };
		thread_local FStencil S;
		if (S.R != R)
		{
			S.R = R;
			S.Offsets.Reset();
			const int32 R2 = R * R;
			for (int32 dy = -R; dy <= R; ++dy)
				for (int32 dx = -R; dx <= R; ++dx)
					if (dx*dx + dy*dy <= R2)
						S.Offsets.Emplace(dx, dy);
		}

		const FIntPoint Center = GetCellCoord2D(Location);
		const float Lx = Location.X, Ly = Location.Y, Lz = Location.Z;
		const float RadiusSq = Radius * Radius;
		const float ZLo = Lz - ZHalfHeight; 
		const float ZHi = Lz + ZHalfHeight;

		int32 Emitted = 0;

		for (const FIntPoint& D : S.Offsets)
		{
			const int64 CellKey = HashCoord(FIntPoint(Center.X + D.X, Center.Y + D.Y));
			const FGridCell* Cell = FindCell(CellKey);
			if (!Cell) continue;

			const int32 Num = Cell->EntityData.Num();
			const FEntityData* RESTRICT Data = Cell->EntityData.GetData();
			for (int32 i = 0; i < Num; ++i)
			{
				if (i + 8 < Num) FPlatformMisc::Prefetch(&Data[i + 8]);

				const FEntityData& E = Data[i];

				const float Ez = E.Location.Z;
				if (Ez < ZLo || Ez > ZHi) continue;

				const float dx = Lx - E.Location.X;
				const float dy = Ly - E.Location.Y;
				if (dx*dx + dy*dy <= RadiusSq)
				{
					if (!Visitor(E)) return;
					if (MaxResults > 0 && ++Emitted >= MaxResults) return;
				}
			}
		}
	}

	FORCEINLINE float GetCellSize() const { return CellSize; }
	FORCEINLINE bool  IsEmpty()    const { return Grid.IsEmpty(); }

	FORCEINLINE int32 EstimateCountAt(const FVector& Location, float Radius) const
	{
		return EstimateCountAt(Location, Radius, TNumericLimits<float>::Max());
	}
	int32 EstimateCountAt(const FVector& Location, float Radius, float ZHalfHeight) const;

private:
	const float CellSize;
	const float InvCellSize;

	using FKV = TestHashTable::TKeyValuePair<int64, FGridCell>;
	TestHashTable::THashTable<int64, FKV, FInt64HashTraits, FUEHashAllocator> Grid;

	FORCEINLINE FIntPoint GetCellCoord2D(const FVector& Location) const
	{
		return FIntPoint(
			FMath::FloorToInt(Location.X * InvCellSize),
			FMath::FloorToInt(Location.Y * InvCellSize));
	}

	static FORCEINLINE int64 HashCoord(const FIntPoint& Coord)
	{
		return static_cast<int64>(Coord.X) * 73856093LL ^ static_cast<int64>(Coord.Y) * 19349663LL;
	}

	const FGridCell* FindCell(int64 Key) const;

	FGridCell& FindOrAddCell(int64 Key);
};
