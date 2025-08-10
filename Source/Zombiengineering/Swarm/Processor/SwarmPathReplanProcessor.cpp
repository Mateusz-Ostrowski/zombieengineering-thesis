#include "SwarmPathReplanProcessor.h"

#include "MassCommonTypes.h"
#include "MassRepresentationTypes.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "Algo/MinElement.h"
#include "NavigationSystem.h"

namespace
{
	struct FPathKey
	{
		FIntVector Start, Goal;
		bool operator==(const FPathKey& O) const { return Start == O.Start && Goal == O.Goal; }
	};
	FORCEINLINE uint32 GetTypeHash(const FPathKey& K)
	{
		return HashCombine(GetTypeHash(K.Start), GetTypeHash(K.Goal));
	}

	struct FCachedPathEntry
	{
		TSharedPtr<const TArray<FVector>> Points;
		double Time = 0.0;
	};

	TMap<FPathKey, FCachedPathEntry> GPathCache;
	TMap<FPathKey, double>           GLastSolveTime;

	constexpr float  GPathCacheCellSize   = 500.f;
	constexpr float  GPathCacheTTL        = 0.9f;
	constexpr double GKeySolveCooldown    = 0.20;
	int32            GPathCacheMaxEntries = 8192;

	FORCEINLINE FIntVector Q3D(const FVector& P)
	{
		return FIntVector(
			FMath::FloorToInt(P.X / GPathCacheCellSize),
			FMath::FloorToInt(P.Y / GPathCacheCellSize),
			FMath::FloorToInt(P.Z / 200.f));
	}

	void GPathCache_RefreshOrInsert(const FPathKey& Key,
		TSharedPtr<const TArray<FVector>> Points,
		double Now)
	{
		FCachedPathEntry& Entry = GPathCache.FindOrAdd(Key);
		Entry.Points = MoveTemp(Points);
		Entry.Time   = Now;
	}

	void GPathCache_EvictOldestIfNeeded()
	{
		if (GPathCache.Num() <= GPathCacheMaxEntries)
			return;

		const double* MinTimePtr = nullptr;
		FPathKey OldestKey;
		for (const TPair<FPathKey, FCachedPathEntry>& It : GPathCache)
		{
			if (!MinTimePtr || It.Value.Time < *MinTimePtr)
			{
				MinTimePtr = &It.Value.Time;
				OldestKey  = It.Key;
			}
		}
		if (MinTimePtr)
		{
			GPathCache.Remove(OldestKey);
		}
	}

	static FORCEINLINE float ComputeCooldown(float Dist, uint32 EntityId)
	{
		const float Near = 200.f, Far = 8000.f;
		const float CdNear = 0.25f, CdFar = 7.5f;

		float BaseCd;
		if (Dist <= Near)      BaseCd = CdNear;
		else if (Dist >= Far)  BaseCd = CdFar;
		else
		{
			const float t = (Dist - Near) / (Far - Near);
			BaseCd = FMath::Lerp(CdNear, CdFar, t);
		}

		const uint32 Seed = EntityId * 2654435761u;
		const float  J    = 0.75f + 0.5f * (float(double(Seed) / double(MAX_uint32)));
		return BaseCd * J;
	}
}

USwarmPathReplanProcessor::USwarmPathReplanProcessor()
	: Query(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Flock);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteInGroup = SwarmGroups::Path;

	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Standalone |
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Client
	);

	RegisterQuery(Query);
}

void USwarmPathReplanProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FSwarmPathStateFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);

	Query.AddRequirement<FSwarmTargetSenseFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FSwarmBudgetStampFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FSwarmUpdatePolicyFragment>(EMassFragmentAccess::ReadOnly);

	Query.AddSharedRequirement<FSwarmMovementParamsFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadOnly);
}

void USwarmPathReplanProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) return;

	const uint32 FrameIdx = (uint32)(World->TimeSeconds * 60.0f);
	bool bRepathBudgetReset = false;

	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (!ShouldProcessChunkThisFrame(Exec, 8))
			return;

		const FSwarmMovementParamsFragment& Params = Exec.GetSharedFragment<FSwarmMovementParamsFragment>();
		FSwarmProfilerSharedFragment& Prof         = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		const FPlayerSharedFragment& Player        = Exec.GetSharedFragment<FPlayerSharedFragment>();

		if (!bRepathBudgetReset)
		{
			Prof.RepathsUsed = 0;
			bRepathBudgetReset = true;
		}

		const float  Dt  = Exec.GetDeltaTimeSeconds();
		const double T0  = FPlatformTime::Seconds();
		const double Now = T0;

		const bool bHaveProjectedGoal =
			Player.bIsOnNavMesh || (FVector::DistSquared(Player.PlayerNavLocation, Player.PlayerLocation) > 1.0f);
		const FVector    FinalGoal  = bHaveProjectedGoal ? Player.PlayerNavLocation : Player.PlayerLocation;
		const FIntVector PlayerCell = Q3D(FinalGoal);

		const int32 N = Exec.GetNumEntities();
		auto Paths       = Exec.GetMutableFragmentView<FSwarmPathStateFragment>();
		auto Xforms      = Exec.GetFragmentView<FTransformFragment>();
		auto Radii       = Exec.GetFragmentView<FAgentRadiusFragment>();
		auto Sense       = Exec.GetFragmentView<FSwarmTargetSenseFragment>();
		auto BudgetStamp = Exec.GetMutableFragmentView<FSwarmBudgetStampFragment>();
		auto Policy      = Exec.GetFragmentView<FSwarmUpdatePolicyFragment>();

		struct FPendingEntity { int32 Index; float DistSq2D; };
		TMap<FPathKey, TArray<FPendingEntity>> Groups;
		Groups.Reserve(FMath::Max(8, N / 8));

		for (int32 i = 0; i < N; ++i)
		{
			FSwarmPathStateFragment& Path = Paths[i];
			const FVector SelfPos = Xforms[i].GetTransform().GetTranslation();

			Path.PathAge        += Dt;
			Path.RepathCooldown  = FMath::Max(0.f, Path.RepathCooldown - Dt);

			if ((FrameIdx & Policy[i].FollowMask) != 0 || !bHaveProjectedGoal)
				continue;

			const bool  bOutOfPath        = !Path.bHasPath || (Path.Index >= Path.NumPoints());
			const bool  bCooldownElapsed  = (Path.RepathCooldown <= 0.f);
			const bool  bCellUnchanged    = (Q3D(Path.LastGoal) == PlayerCell);
			const float DistSq2D          = FVector::DistSquared2D(SelfPos, FinalGoal);
			const bool  bGoalMovedEnough  = !bCellUnchanged &&
				(FVector::DistSquared(Path.LastGoal, FinalGoal) > FMath::Square(2.0f * Radii[i].Radius));

			const bool bOnLastSegment = Path.bHasPath &&
				(Path.NumPoints() <= 2 || Path.Index >= FMath::Max(1, Path.NumPoints() - 2));
			const bool bNearEnd = bOnLastSegment && (DistSq2D <= FMath::Square(Params.EndOfPathRepathRadius));
			Path.NoLOSTime = (bNearEnd && !Sense[i].bLOS) ? (Path.NoLOSTime + Dt) : 0.f;
			const bool bForceRepathNearEndNoLOS = bNearEnd && (Path.NoLOSTime > 0.25f);

			const bool bIdleStaleness = (Path.PathAge >= 2.5f);

			if (bOutOfPath || (bCooldownElapsed && bGoalMovedEnough) || bForceRepathNearEndNoLOS || bIdleStaleness)
			{
				const FPathKey Key{ Q3D(SelfPos), PlayerCell };
				Groups.FindOrAdd(Key).Add({ i, DistSq2D });
			}
		}

		if (Groups.Num() == 0 || Prof.RepathsUsed >= Params.RepathsPerFrameBudget)
		{
			Prof.T_PathReplan += (FPlatformTime::Seconds() - T0) * 1000.0;
			return;
		}

		for (auto& Pair : Groups)
		{
			if (Prof.RepathsUsed >= Params.RepathsPerFrameBudget)
				break;

			const FPathKey& Key = Pair.Key;
			auto& Members = Pair.Value;
			TSharedPtr<const TArray<FVector>> SharedRef;

			if (FCachedPathEntry* Found = GPathCache.Find(Key))
			{
				if ((Now - Found->Time) <= GPathCacheTTL && Found->Points && Found->Points->Num() >= 2)
				{
					SharedRef = Found->Points;
					Found->Time = Now;
				}
			}

			if (!SharedRef)
			{
				if (double* LastT = GLastSolveTime.Find(Key))
				{
					if ((Now - *LastT) < GKeySolveCooldown)
						continue;
				}
			}
			
			if (!SharedRef)
			{
				const FPendingEntity& Rep = *Algo::MinElementBy(Members, &FPendingEntity::DistSq2D);
				const FVector RepStart = Xforms[Rep.Index].GetTransform().GetTranslation();

				if (UNavigationPath* P = NavSys->FindPathToLocationSynchronously(World, RepStart, FinalGoal))
				{
					if (P->PathPoints.Num() >= 2 && Prof.RepathsUsed < Params.RepathsPerFrameBudget)
					{
						SharedRef = MakeShared<TArray<FVector>>(MoveTemp(P->PathPoints));
						++Prof.RepathsUsed;
						GPathCache_RefreshOrInsert(Key, SharedRef, Now);
						GPathCache_EvictOldestIfNeeded();
						GLastSolveTime.FindOrAdd(Key) = Now;
					}
				}
			}

			if (SharedRef)
			{
				for (const FPendingEntity& PE : Members)
				{
					FSwarmPathStateFragment& Path = Paths[PE.Index];
					Path.PointsRef = SharedRef;
					Path.Index     = 1;
					Path.bHasPath  = true;
					Path.LastGoal  = FinalGoal;
					Path.RepathCooldown = ComputeCooldown(FMath::Sqrt(PE.DistSq2D), Exec.GetEntity(PE.Index).AsNumber());
					Path.PathAge = 0.f;
					BudgetStamp[PE.Index].bDidReplan = true;
				}
			}
		}

		Prof.T_PathReplan += (FPlatformTime::Seconds() - T0) * 1000.0;
	});
}
