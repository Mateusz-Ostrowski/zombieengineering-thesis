#include "SwarmBuildSpatialGridProcessor.h"

#include "Engine/World.h"
#include "MassCommonFragments.h" 
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "Swarm/Fragment/SwarmTypes.h"
#include "Swarm/Grid/SwarmGridSubsystem.h"
#include "SwarmProcessorCommons.h"

USwarmBuildSpatialGridProcessor::USwarmBuildSpatialGridProcessor()
	: Query(*this)
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Sense);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteInGroup = SwarmGroups::Prepare;
}

void USwarmBuildSpatialGridProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void USwarmBuildSpatialGridProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World)
	{
		return;
	}

	USwarmGridSubsystem* GridSS = World->GetSubsystem<USwarmGridSubsystem>();
	if (!GridSS)
	{
		return;
	}

	GridSS->ResetGrid();

	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		FSwarmProfilerSharedFragment& Prof = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		const double T0 = FPlatformTime::Seconds();

		const int32 N = Exec.GetNumEntities();

		auto Transforms = Exec.GetFragmentView<FTransformFragment>();

		for (int32 i = 0; i < N; ++i)
		{
			const FVector Position = Transforms[i].GetTransform().GetLocation();
			GridSS->InsertEntity(Exec.GetEntity(i), Position);
		}

		Prof.T_BuildGrid += (FPlatformTime::Seconds() - T0) * 1000.0;
	});
}
