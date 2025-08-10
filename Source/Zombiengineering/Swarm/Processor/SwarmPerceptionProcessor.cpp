#include "SwarmPerceptionProcessor.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "CollisionQueryParams.h"
#include "MassAIBehaviorTypes.h"
#include "HAL/PlatformTime.h"

#include "MassCommonFragments.h"
#include "MassExecutionContext.h"
#include "SwarmProcessorCommons.h"
#include "Swarm/Fragment/SwarmTypes.h"

USwarmPerceptionProcessor::USwarmPerceptionProcessor()
	: Query(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Path);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteInGroup = SwarmGroups::Sense;

	RegisterQuery(Query);
}

void USwarmPerceptionProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FSwarmLOSFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);

	Query.AddRequirement<FSwarmTargetSenseFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FSwarmBudgetStampFragment>(EMassFragmentAccess::ReadWrite);

	Query.AddRequirement<FSwarmUpdatePolicyFragment>(EMassFragmentAccess::ReadOnly);

	Query.AddSharedRequirement<FSwarmMovementParamsFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);

}

void USwarmPerceptionProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World) return;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!PlayerPawn) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	const uint32 FrameIdx = (uint32)(World->TimeSeconds * 60.0f);

	const double T0 = FPlatformTime::Seconds();
	Query.ParallelForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		FSwarmProfilerSharedFragment& Prof          = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		const FPlayerSharedFragment& Player         = Exec.GetSharedFragment<FPlayerSharedFragment>();
		const FSwarmMovementParamsFragment& Params  = Exec.GetSharedFragment<FSwarmMovementParamsFragment>();

		if (LastLOSResetFrame != FrameIdx)
		{
			Prof.LOSChecksUsed = 0;
			LastLOSResetFrame  = FrameIdx;
		}


		const int32 N = Exec.GetNumEntities();
		auto Xforms      = Exec.GetFragmentView<FTransformFragment>();
		auto LOS         = Exec.GetMutableFragmentView<FSwarmLOSFragment>();
		auto Sense       = Exec.GetMutableFragmentView<FSwarmTargetSenseFragment>();
		auto Stamp       = Exec.GetMutableFragmentView<FSwarmBudgetStampFragment>();
		auto Policy      = Exec.GetFragmentView<FSwarmUpdatePolicyFragment>();

		const float Dt = Exec.GetDeltaTimeSeconds();

		const FVector PlayerLoc    = Player.PlayerLocation;
		const FVector PlayerNavLoc = Player.PlayerNavLocation;
		const bool    bPlayerOnNav = Player.bIsOnNavMesh;
		const FVector ZOffset(0.f, 0.f, Params.LOSHeightOffset);
		const float   DirectChaseRangeSq = FMath::Square(Params.DirectChaseRange);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SwarmPlayerLOS), false);
		QueryParams.bReturnPhysicalMaterial = false;

		auto ComputeLOS = [&](const FVector& From) -> bool
		{
			if (bPlayerOnNav)
			{
				FNavLocation FromNav;
				if (NavSys->ProjectPointToNavigation(From, FromNav, FVector(50, 50, 100)))
				{
					FVector HitLoc;
					const bool bBlocked = NavSys->NavigationRaycast(World, FromNav.Location, PlayerNavLoc, HitLoc, nullptr, nullptr);
					return !bBlocked;
				}
			}

			const FVector Start = From + ZOffset;
			const FVector End   = PlayerLoc + ZOffset;
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, QueryParams);
			return (!bHit || Hit.GetActor() == PlayerPawn);
		};

		for (int32 i = 0; i < N; ++i)
		{
			Stamp[i].bDidReplan     = false;
			Stamp[i].bDidLOSRefresh = false;

			const FVector MyLoc = Xforms[i].GetTransform().GetLocation();
			Sense[i].TargetLocation = PlayerLoc;

			LOS[i].TimeSinceRefresh += Dt;

			const bool bSenseThisFrame  = ((FrameIdx & Policy[i].SenseMask) == 0);
			const bool bInChaseRange    = (Policy[i].DistToPlayer2D_Sq <= DirectChaseRangeSq);

			if (!bSenseThisFrame || !bInChaseRange)
			{
				Sense[i].bLOS        = LOS[i].bHasLOS;
				Sense[i].bLOSUpdated = false;
				continue;
			}

			const uint32 H      = GetTypeHash(Exec.GetEntity(i));
			const float  Phase  = (H & 0xFF) * (Params.LOSRefreshSeconds / 256.f);
			const bool   bDue   = (LOS[i].TimeSinceRefresh + Phase >= Params.LOSRefreshSeconds);
			bool         bLOSNow = LOS[i].bHasLOS;

			if (bDue && (Prof.LOSChecksUsed < Params.LOSChecksPerFrameBudget))
			{
				++Prof.LOSChecksUsed;
				LOS[i].TimeSinceRefresh = 0.f;

				bLOSNow = ComputeLOS(MyLoc);
				LOS[i].bHasLOS = bLOSNow;
				Stamp[i].bDidLOSRefresh = true;
			}

			Sense[i].bLOS        = bLOSNow;
			Sense[i].bLOSUpdated = Stamp[i].bDidLOSRefresh;
		}

	}, FMassEntityQuery::EParallelExecutionFlags::Force);

	bool b = false;
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (b) return;
		FSwarmProfilerSharedFragment& Prof         = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		Prof.T_Perception = (FPlatformTime::Seconds() - T0) * 1000.0;
		b = true;
	});
}
