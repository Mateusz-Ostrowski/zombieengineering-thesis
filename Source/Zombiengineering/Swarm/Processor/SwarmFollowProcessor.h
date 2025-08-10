#pragma once
#include "MassProcessor.h"
#include "SwarmFollowProcessor.generated.h"

UCLASS()
class USwarmFollowProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USwarmFollowProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager&, FMassExecutionContext& Context) override;

	static FORCEINLINE int64 MakeBucketKey(const FVector& P, float Cell);
	static int32 FindNearestPointIndex2D(const TArray<FVector>& Pts, const FVector& Pos);

	bool TryRequestReplanBudgeted(
		FMassExecutionContext& Exec,
		const FMassEntityHandle Entity,
		const FVector& From,
		const FVector& Goal,
		const int64 BucketKey,
		const bool bUseHierarchical);

private:
	FMassEntityQuery FollowQuery;

	float ReplanBucketCellSize = 2500.f;
	float ReplanPlayerMoveThreshold = 120.f;

	FCriticalSection PathCS;
	TMap<int64, TArray<FVector>> PendingBucketResults_GT;
	TSet<int64> InFlightBuckets_GT;

	TMap<int64, TArray<FVector>> BucketResults_RT;
	uint32 LastResultsFrame = 0;

	FCriticalSection BudgetCS;
	int32 MaxBucketsPerFrame = 32;
	int32 BucketsScheduledThisFrame = 0;
	uint32 LastBudgetResetFrame = 0;

	uint32 HCache = 0;
};
