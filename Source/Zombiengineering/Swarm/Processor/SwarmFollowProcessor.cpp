#include "SwarmFollowProcessor.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Async/Async.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassNavigationFragments.h"
#include "SwarmProcessorCommons.h"
#include "Swarm/Fragment/SwarmTypes.h"

FORCEINLINE int64 USwarmFollowProcessor::MakeBucketKey(const FVector& P, float Cell)
{
	const int32 X = FMath::FloorToInt(P.X / Cell);
	const int32 Y = FMath::FloorToInt(P.Y / Cell);
	return (static_cast<int64>(X) << 32) ^ static_cast<int64>(Y);
}
int32 USwarmFollowProcessor::FindNearestPointIndex2D(const TArray<FVector>& Pts, const FVector& Pos)
{
	if (Pts.Num() <= 1) return 0;
	int32 BestIdx = 1;
	float BestDsq = TNumericLimits<float>::Max();
	for (int32 k = 1; k < Pts.Num(); ++k)
	{
		const float D = FVector::DistSquared2D(Pts[k], Pos);
		if (D < BestDsq) { BestDsq = D; BestIdx = k; }
	}
	return BestIdx;
}

USwarmFollowProcessor::USwarmFollowProcessor()
	: FollowQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = SwarmGroups::Follow;
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Integrate);

	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Standalone |
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Client);

	RegisterQuery(FollowQuery);
}

void USwarmFollowProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	FollowQuery.AddRequirement<FSwarmPathStateFragment>(EMassFragmentAccess::ReadWrite);
	FollowQuery.AddRequirement<FSwarmSeparationFragment>(EMassFragmentAccess::ReadWrite);
	FollowQuery.AddRequirement<FSwarmPathWindowFragment>(EMassFragmentAccess::ReadWrite);
	FollowQuery.AddRequirement<FSwarmBudgetStampFragment>(EMassFragmentAccess::ReadWrite);
	FollowQuery.AddRequirement<FSwarmUpdatePolicyFragment>(EMassFragmentAccess::ReadOnly);
	FollowQuery.AddRequirement<FSwarmTargetSenseFragment>(EMassFragmentAccess::ReadOnly);
	FollowQuery.AddRequirement<FSwarmAgentFragment>(EMassFragmentAccess::ReadOnly);
	FollowQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);

	FollowQuery.AddSharedRequirement<FSwarmMovementParamsFragment>(EMassFragmentAccess::ReadOnly);
	FollowQuery.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadOnly);
	FollowQuery.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void USwarmFollowProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World) return;

	const uint32 FrameIdx = (uint32)(World->TimeSeconds * 60.0f);

	{
		FScopeLock L(&PathCS);
		if (LastResultsFrame != FrameIdx)
		{
			BucketResults_RT.Reset();
			for (auto& Pair : PendingBucketResults_GT)
				BucketResults_RT.Add(Pair.Key, MoveTemp(Pair.Value));
			PendingBucketResults_GT.Reset();
			LastResultsFrame = FrameIdx;
		}
	}
	const TMap<int64, TArray<FVector>>& LocalBucketResults = BucketResults_RT;

	{
		FScopeLock L(&BudgetCS);
		if (LastBudgetResetFrame != FrameIdx)
		{
			LastBudgetResetFrame = FrameIdx;
			BucketsScheduledThisFrame = 0;
		}
	}

	FollowQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (!ShouldProcessChunkThisFrame(Exec)) return;

		const FSwarmMovementParamsFragment& Params = Exec.GetSharedFragment<FSwarmMovementParamsFragment>();
		FSwarmProfilerSharedFragment& Prof         = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();

		const int32 N = Exec.GetNumEntities();
		if (N == 0) return;

		auto Paths       = Exec.GetMutableFragmentView<FSwarmPathStateFragment>();
		auto Steer       = Exec.GetMutableFragmentView<FSwarmSeparationFragment>();
		auto PathWindow  = Exec.GetMutableFragmentView<FSwarmPathWindowFragment>();
		auto BudgetStamp = Exec.GetMutableFragmentView<FSwarmBudgetStampFragment>();
		auto Policy      = Exec.GetFragmentView<FSwarmUpdatePolicyFragment>();
		auto Sense       = Exec.GetFragmentView<FSwarmTargetSenseFragment>();
		auto Agents    = Exec.GetFragmentView<FSwarmAgentFragment>();
		auto Transforms    = Exec.GetFragmentView<FTransformFragment>();

		const float  Dt = FMath::Clamp(Exec.GetDeltaTimeSeconds(), 0.f, 0.05f);
		const double T0 = FPlatformTime::Seconds();

		auto IsPathFresh = [&](int i, const FVector& selfPos)
		{
			const float speed = Params.MaxSpeed;
			const float dist  = Paths[i].bHasPath
				? FVector::Dist2D(selfPos, Paths[i].LastGoal)
				: FVector::Dist2D(selfPos, Sense[i].TargetLocation);

			const float travelMs     = (dist / FMath::Max(1.f, speed * 0.60f)) * 1000.f;
			const float maxPathAgeMs = FMath::Clamp(travelMs, 2000.f, 10000.f);

			return Paths[i].bHasPath
				&& (Paths[i].PathAge * 1000.0f <= maxPathAgeMs)
				&& (Paths[i].Index >= 0)
				&& (Paths[i].Index < Paths[i].NumPoints());
		};

		auto AdvanceWaypointIfClose = [&](int i, const FVector& selfPos, FVector& target)
		{
			if (FVector::DistSquared2D(selfPos, target) > FMath::Square(Params.WaypointAcceptanceRadius))
				return;

			++Paths[i].Index;
			Paths[i].Index   = FMath::Clamp(Paths[i].Index, 0, FMath::Max(0, Paths[i].NumPoints() - 1));
			Paths[i].PathAge = 0.f;

			if (Paths[i].Index < Paths[i].NumPoints())
				target = Paths[i].Point(Paths[i].Index);
		};

		auto BuildSmallWindowIfAllowed = [&](int i)
		{
			if ((FrameIdx & Policy[i].FollowMask) != 0)
				return;

			const int32 num = Paths[i].NumPoints();
			const auto clampIdx = [&](int idx) { return FMath::Clamp(idx, 0, FMath::Max(0, num - 1)); };

			const int32 i0 = clampIdx(Paths[i].Index);
			const int32 i1 = clampIdx(i0 + 1);
			const int32 i2 = clampIdx(i1 + 1);

			PathWindow[i].P0 = Paths[i].Point(i0);
			PathWindow[i].P1 = Paths[i].Point(i1);
			PathWindow[i].P2 = Paths[i].Point(i2);

			const FVector2D v01(PathWindow[i].P1 - PathWindow[i].P0);
			const FVector2D v12(PathWindow[i].P2 - PathWindow[i].P1);
			const float l01Sq = v01.SquaredLength();
			const float l12Sq = v12.SquaredLength();

			FVector2D t2d(0.f, 0.f);
			float     curv = 0.f;

			if (l01Sq > 1e-6f)
			{
				const float invL01 = FMath::InvSqrt(l01Sq);
				t2d = v01 * invL01;

				if (l12Sq > 1e-6f)
				{
					const float invL12 = FMath::InvSqrt(l12Sq);
					const float cross  = v01.X * v12.Y - v01.Y * v12.X;
					const float sinTh  = FMath::Abs(cross) * invL01 * invL12;
					curv = sinTh * invL01;
				}
			}

			PathWindow[i].Tangent2D = FVector(t2d.X, t2d.Y, 0.f);
			PathWindow[i].Curvature = curv;
			PathWindow[i].bValid    = 1;
		};

		for (int32 i = 0; i < N; ++i)
		{
			const FVector selfPos = Transforms[i].GetTransform().GetLocation();

			if (Paths[i].bHasPath && Paths[i].Index >= Paths[i].NumPoints())
				Paths[i].bHasPath = false;

			bool bFresh = IsPathFresh(i, selfPos);
			const int64 bucketKey = MakeBucketKey(selfPos, ReplanBucketCellSize);

			if (!bFresh)
			{
				if (const TArray<FVector>* bucketPts = LocalBucketResults.Find(bucketKey))
				{
					TSharedPtr<TArray<FVector>> shared = MakeShared<TArray<FVector>>(*bucketPts);
					Paths[i].PointsRef = shared;
					Paths[i].Index     = FMath::Clamp(
						FindNearestPointIndex2D(*shared, selfPos), 1, FMath::Max(1, Paths[i].NumPoints() - 1));
					Paths[i].bHasPath  = (Paths[i].NumPoints() > 1);
					Paths[i].PathAge   = 0.f;
					Paths[i].LastGoal  = Sense[i].TargetLocation;
					bFresh = Paths[i].bHasPath;
				}
			}

			if (!bFresh)
			{
				const float playerMoved2D = FVector::Dist2D(Sense[i].TargetLocation, Paths[i].LastGoal);
				const bool  bShouldReplan =
					(!Paths[i].bHasPath) ||
					(Paths[i].Index >= Paths[i].NumPoints()) ||
					(playerMoved2D >= ReplanPlayerMoveThreshold);

				if (bShouldReplan && Paths[i].RepathCooldown <= 0.f && ((FrameIdx & Policy[i].FollowMask) == 0))
				{
					const float distToGoal = FVector::Dist2D(selfPos, Sense[i].TargetLocation);
					const bool  bUseHier   = (distToGoal > 3000.f);

					if (TryRequestReplanBudgeted(Exec, Exec.GetEntity(i), selfPos, Sense[i].TargetLocation, bucketKey, bUseHier))
					{
						Paths[i].RepathCooldown   = 0.25f;
						BudgetStamp[i].bDidReplan = true;
						++Prof.RepathsUsed;
					}
				}

				Paths[i].PathAge += Dt;
				if (Paths[i].RepathCooldown > 0.f)
					Paths[i].RepathCooldown = FMath::Max(0.f, Paths[i].RepathCooldown - Dt);

				Prof.AvgPathAgeAccum += Paths[i].PathAge;
				Prof.AvgPathAgeNum   += 1;
				continue;
			}

			FVector target = Paths[i].Point(Paths[i].Index);
			AdvanceWaypointIfClose(i, selfPos, target);

			const bool bOnLastSegment = (Paths[i].NumPoints() <= 2) ||
				(Paths[i].Index >= FMath::Max(1, Paths[i].NumPoints() - 2));
			const bool bClose = (FVector::DistSquared2D(selfPos, Sense[i].TargetLocation)
				<= FMath::Square(Params.DirectChaseRange));
			const bool bDirect = (Sense[i].bLOS && bOnLastSegment && bClose);

			if (bDirect)
			{
				target = Sense[i].TargetLocation;
				Paths[i].LastGoal    = Sense[i].TargetLocation;
				PathWindow[i].bValid = 0;
				++Prof.DirectChaseCount;
			}
			else
			{
				BuildSmallWindowIfAllowed(i);
			}

			const float distToTarget = FVector::Dist2D(selfPos, target);
			FVector pathDir = (target - selfPos).GetSafeNormal2D();

			if (!bDirect && distToTarget > Params.PathSpreadMinDistance)
			{
				const float clamped = FMath::Min(distToTarget, Params.PathSpreadMaxDistance);
				const float alpha   = (clamped - Params.PathSpreadMinDistance) /
					FMath::Max(1.f, (Params.PathSpreadMaxDistance - Params.PathSpreadMinDistance));

				const float spread = Params.PathSpreadMaxOffset * alpha * Agents[i].LaneMag * Agents[i].LaneSign;
				if (spread != 0.f)
				{
					FVector2D t2d(pathDir.X, pathDir.Y);
					const float invLen = FMath::InvSqrt(FMath::Max(1e-4f, t2d.SquaredLength()));
					t2d *= invLen;
					const FVector2D right2d(-t2d.Y, t2d.X);
					pathDir = (target + FVector(right2d.X, right2d.Y, 0.f) * spread - selfPos).GetSafeNormal2D();
				}
			}

			const float dens = Steer[i].LocalDensity;
			const float deemphasis = (dens >= 6.f) ? 0.6f : (dens >= 3.f ? 0.8f : 1.f);

			Steer[i].PathDir    = pathDir;
			Steer[i].PathWeight = Exec.GetSharedFragment<FSwarmMovementParamsFragment>().PathFollowWeight * deemphasis;

			Paths[i].PathAge += Dt;
			if (Paths[i].RepathCooldown > 0.f)
				Paths[i].RepathCooldown = FMath::Max(0.f, Paths[i].RepathCooldown - Dt);

			Prof.AvgPathAgeAccum += Paths[i].PathAge;
			Prof.AvgPathAgeNum   += 1;
		}

		Prof.T_PathFollow += (FPlatformTime::Seconds() - T0) * 1000.0;
	});
}

bool USwarmFollowProcessor::TryRequestReplanBudgeted(
	FMassExecutionContext& Exec,
	const FMassEntityHandle,
	const FVector& From,
	const FVector& Goal,
	const int64 BucketKey,
	const bool bUseHierarchical)
{
	{
	FScopeLock L(&PathCS);
	if (PendingBucketResults_GT.Contains(BucketKey) || InFlightBuckets_GT.Contains(BucketKey))
		return true;
	}
	{
	FScopeLock L(&BudgetCS);
	if (BucketsScheduledThisFrame >= MaxBucketsPerFrame)
		return false;
	++BucketsScheduledThisFrame;
	}

	UWorld* World = Exec.GetWorld();
	if (!World) return false;

	AsyncTask(ENamedThreads::GameThread, [this, World, From, Goal, BucketKey, bUseHierarchical]()
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!NavSys) return;

		const ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (!NavData) return;

		{
			FScopeLock L(&PathCS);
			if (InFlightBuckets_GT.Contains(BucketKey)) return;
			InFlightBuckets_GT.Add(BucketKey);
		}

		FNavLocation FromNav, GoalNav;
		if (!NavSys->ProjectPointToNavigation(From, FromNav, FVector(100,100,200), NavData) ||
			!NavSys->ProjectPointToNavigation(Goal, GoalNav, FVector(100,100,200), NavData))
		{
			FScopeLock L(&PathCS);
			InFlightBuckets_GT.Remove(BucketKey);
			return;
		}

		FPathFindingQuery PFQ(nullptr, *NavData, FromNav.Location, GoalNav.Location);
		const FNavDataConfig& cfg = NavData->GetConfig();
		FNavAgentProperties agentProps{ };
		agentProps.AgentRadius = cfg.AgentRadius;
		agentProps.AgentHeight = cfg.AgentHeight;

		NavSys->FindPathAsync(
			agentProps, PFQ,
			FNavPathQueryDelegate::CreateWeakLambda(this,
				[this, BucketKey](uint32, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path)
				{
					TArray<FVector> pts;
					if (Result == ENavigationQueryResult::Success && Path.IsValid())
					{
						const auto& P = Path->GetPathPoints();
						if (P.Num() >= 2)
						{
							pts.Reserve(P.Num());
							for (const FNavPathPoint& PP : P) pts.Add(PP.Location);
						}
					}

					FScopeLock L(&PathCS);
					InFlightBuckets_GT.Remove(BucketKey);
					if (pts.Num() > 1)
						PendingBucketResults_GT.Add(BucketKey, MoveTemp(pts));
				}),
			bUseHierarchical ? EPathFindingMode::Hierarchical : EPathFindingMode::Regular
		);
	});

	return true;
}
