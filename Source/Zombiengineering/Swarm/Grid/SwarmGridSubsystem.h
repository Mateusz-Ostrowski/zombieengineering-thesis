#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Swarm/Grid/AgentSpatialHashGrid.h"
#include "Templates/UnrealTemplate.h"
#include "SwarmGridSubsystem.generated.h"

UCLASS()
class USwarmGridSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable)
	FORCEINLINE float GetCellSize() const { return CellSize; }

	FORCEINLINE bool IsGridEmpty() const { return !Grid || Grid->IsEmpty(); }

	FORCEINLINE FAgentSpatialHashGrid& GetGrid() const { return *Grid; }

	FORCEINLINE void ResetGrid() const
	{
		Grid->Reset();
	}

	FORCEINLINE void InsertEntity(const FMassEntityHandle& Entity, const FVector& Location) const
	{
		Grid->InsertEntity(Entity, Location);
	}
	
	FORCEINLINE void QueryNearby(const FVector& Location, float Radius, TArray<FEntityData, TInlineAllocator<16>>& OutEntities, int32 MaxResults = -1) const
	{
		Grid->QueryNearby(Location, Radius, OutEntities, MaxResults);
	}

	FORCEINLINE void QueryNearby(const FVector& Location, float Radius, float ZHalfHeight, TArray<FEntityData, TInlineAllocator<16>>& OutEntities, int32 MaxResults = -1) const
	{
		Grid->QueryNearby(Location, Radius, ZHalfHeight, OutEntities, MaxResults);
	}

	template <typename FVisitor>
	FORCEINLINE void VisitNearby(const FVector& Location, float Radius, float ZHalfHeight, int32 MaxResults, FVisitor&& Visitor) const
	{
		Grid->VisitNearby(Location, Radius, ZHalfHeight, MaxResults, Forward<FVisitor>(Visitor));
	}

	FORCEINLINE int32 EstimateCountAt(const FVector& Location, float Radius) const
	{
		return Grid->EstimateCountAt(Location, Radius);
	}

	FORCEINLINE int32 EstimateCountAt(const FVector& Location, float Radius, float ZHalfHeight) const
	{
		return Grid->EstimateCountAt(Location, Radius, ZHalfHeight);
	}

public:
	UPROPERTY() float CellSize = 200.f;

private:
	TUniquePtr<FAgentSpatialHashGrid> Grid;
};
