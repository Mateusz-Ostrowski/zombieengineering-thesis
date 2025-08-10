#include "SwarmUpdatePolicyProcessor.h"

#include "MassCommonFragments.h"
#include "HAL/PlatformTime.h"
#include "Swarm/Grid/SwarmGridSubsystem.h"
#include "Swarm/Fragment/SwarmTypes.h"
#include "SwarmProcessorCommons.h"

USwarmUpdatePolicyProcessor::USwarmUpdatePolicyProcessor()
	: Query(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteAfter.Add(SwarmGroups::Prepare);
	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Sense);
	ExecutionOrder.ExecuteInGroup = SwarmGroups::PrePass;

	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Standalone |
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Client
	);

	RegisterQuery(Query);
}

void USwarmUpdatePolicyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FSwarmUpdatePolicyFragment>(EMassFragmentAccess::ReadWrite);

	Query.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void USwarmUpdatePolicyProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World) return;

	USwarmGridSubsystem* GridSS = World->GetSubsystem<USwarmGridSubsystem>();
	if (!GridSS) return;

	const bool  bGridEmpty = GridSS->IsGridEmpty();
	const float CellSize   = GridSS->GetCellSize();

	const float AreaM2PerCell = FMath::Max(1e-3f, (CellSize * CellSize) * 0.0001f);
	const float CountRadius   = 0.6f * CellSize;
	const float ZHalfHeight   = 120.f;

	const float NearSq = FMath::Square(1500.f);
	const float FarSq  = FMath::Square(4000.f);

	const float Dense     = 3.0f;
	const float VeryDense = 6.0f;

	const double T0 = FPlatformTime::Seconds();
	
	Query.ParallelForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (!ShouldProcessChunkThisFrame(Exec, 30))
			return;
		
		const FPlayerSharedFragment& Player = Exec.GetSharedFragment<FPlayerSharedFragment>();

		const int32 N = Exec.GetNumEntities();
		auto Transforms   = Exec.GetFragmentView<FTransformFragment>();
		auto Policy   = Exec.GetMutableFragmentView<FSwarmUpdatePolicyFragment>();

		for (int32 i = 0; i < N; ++i)
		{
			const FVector P   = Transforms[i].GetTransform().GetLocation();
			const float   d2  = FVector::DistSquared2D(P, Player.PlayerLocation);

			int32 CountInArea = 0;
			if (!bGridEmpty)
			{
				CountInArea = GridSS->EstimateCountAt(P, CountRadius, ZHalfHeight);
			}
			const float Density = (CountInArea > 0) ? (CountInArea / AreaM2PerCell) : 0.0f;

			uint8 FlockMask  = 0;
			uint8 FollowMask = 0;
			uint8 SenseMask  = 0;

			if      (Density >= VeryDense)
			{
				FlockMask = 0x3;
			}
			else if (Density >= Dense)
			{
				FlockMask = 0x1;
			}

			if      (d2 >= FarSq)
			{
				FollowMask = 0x3; SenseMask = 0x7;
			}
			else if (d2 >= NearSq)
			{
				FollowMask = 0x1; SenseMask = 0x1;
			}

			float CooldownScale = 1.0f;
			if (d2 >= NearSq)
			{
				CooldownScale *= (d2 >= FarSq ? 2.0f : 1.5f);
			}
			if (Density >= VeryDense)
			{
				CooldownScale *= 1.5f;
			}

			FSwarmUpdatePolicyFragment& Out = Policy[i];
			Out.DistToPlayer2D_Sq = d2;
			Out.EstimatedDensity  = Density;
			Out.CooldownScale     = CooldownScale;
			Out.SeparationMask         = FlockMask;
			Out.FollowMask        = FollowMask;
			Out.SenseMask         = SenseMask;
		}

	}, FMassEntityQuery::EParallelExecutionFlags::Force);

	bool b = false;
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (b) return;
		FSwarmProfilerSharedFragment& Prof  = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		Prof.T_UpdatePolicy = (FPlatformTime::Seconds() - T0) * 1000.0;
		b = true;
	});
}
