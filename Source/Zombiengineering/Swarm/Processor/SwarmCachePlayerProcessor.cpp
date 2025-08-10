#include "SwarmCachePlayerProcessor.h"

#include "MassCommonTypes.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "SwarmProcessorCommons.h"
#include "Kismet/GameplayStatics.h"
#include "Swarm/Fragment/SwarmTypes.h"

USwarmCachePlayerProcessor::USwarmCachePlayerProcessor()
	: Query(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteInGroup = SwarmGroups::Prepare;
	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Sense);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	bRequiresGameThreadExecution = true;

	RegisterQuery(Query);
}

void USwarmCachePlayerProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void USwarmCachePlayerProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	const double ExecStartS = FPlatformTime::Seconds();

	UWorld* World = Context.GetWorld();
	if (!World) return;

	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!PlayerPawn) return;

	const FVector RawLoc = PlayerPawn->GetActorLocation();

	FVector Projected = RawLoc;
	bool bIsOnNavMesh = false;

	const FVector NavProjectExtent = FVector(3000.f, 3000.f, 10000.f);
	const float XYNavMeshTolerance = 5.f;
	const float ZNavMeshTolerance = 50.f;
	
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		ANavigationData* NavData = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		FNavLocation Out;
		if ( Nav->ProjectPointToNavigation(RawLoc, Out, NavProjectExtent, NavData, nullptr))
		{
			Projected = Out.Location;

			const float XYDistSq = FVector::DistSquared2D(RawLoc, Projected);
			const float ZDist    = FMath::Abs(RawLoc.Z - Projected.Z);
			bIsOnNavMesh = (XYDistSq <= FMath::Square(XYNavMeshTolerance)) && (ZDist <= ZNavMeshTolerance);
		}
	}
	
	const double Now = World->GetTimeSeconds();

	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		FPlayerSharedFragment& Shared = Exec.GetMutableSharedFragment<FPlayerSharedFragment>();
		if (Shared.LastUpdateSeconds == Now) return;

		Shared.PlayerLocation    = RawLoc;
		Shared.PlayerLocation2D  = FVector2D(RawLoc.X, RawLoc.Y);
		Shared.PlayerNavLocation = Projected;
		Shared.bIsOnNavMesh      = bIsOnNavMesh;
		Shared.LastUpdateSeconds = Now;

		FSwarmProfilerSharedFragment& P = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		P.T_PlayerCache += (FPlatformTime::Seconds() - ExecStartS) * 1000.0;
	});
}
