#include "SwarmLocalSeparationProcessor.h"

#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassRepresentationTypes.h"
#include "SwarmProcessorCommons.h"
#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "Swarm/Fragment/SwarmTypes.h"
#include "Swarm/Grid/AgentSpatialHashGrid.h"
#include "Swarm/Grid/SwarmGridSubsystem.h"

USwarmLocalSeparationProcessor::USwarmLocalSeparationProcessor()
	: Query(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Integrate);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteInGroup = SwarmGroups::Flock;

	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Standalone |
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Client
	);

	RegisterQuery(Query);
}

void USwarmLocalSeparationProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FSwarmSeparationFragment>(EMassFragmentAccess::ReadWrite);

	Query.AddRequirement<FSwarmUpdatePolicyFragment>(EMassFragmentAccess::ReadOnly);

	Query.AddSharedRequirement<FSwarmMovementParamsFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void USwarmLocalSeparationProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World) return;

	USwarmGridSubsystem* GridSS = World->GetSubsystem<USwarmGridSubsystem>();
	if (!GridSS) return;

	const uint32 FrameIdx = static_cast<uint32>(World->TimeSeconds * 60.0f);
	constexpr float ZHalfHeight = 120.f;
	constexpr float Skin        = 10.f;

	const double T0 = FPlatformTime::Seconds();
	Query.ParallelForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (!ShouldProcessChunkThisFrame(Exec, 3)) return;

		const FSwarmMovementParamsFragment& Params = Exec.GetSharedFragment<FSwarmMovementParamsFragment>();

		const int32 N = Exec.GetNumEntities();
		if (N == 0) return;

		auto Xforms     = Exec.GetFragmentView<FTransformFragment>();
		auto Separation = Exec.GetMutableFragmentView<FSwarmSeparationFragment>();
		auto Policy     = Exec.GetFragmentView<FSwarmUpdatePolicyFragment>();

		TArray<FVector, TInlineAllocator<256>> Pos;
		Pos.SetNumUninitialized(N);
		for (int32 i = 0; i < N; ++i)
			Pos[i] = Xforms[i].GetTransform().GetLocation();

		const float QueryR      = Params.NeighborRadius;
		const float QueryAreaM2 = FMath::Max(1e-6f, PI * (QueryR * QueryR) * 0.0001f);
		const float Mid2 = FMath::Square(1500.f);
		const float Far2 = FMath::Square(3000.f);

		auto ShouldSkip = [&](int32 Idx) -> bool
		{
			const uint32 H = GetTypeHash(Exec.GetEntity(Idx));
			const float  d2 = Policy[Idx].DistToPlayer2D_Sq;

			if (d2 > Far2) { if (((FrameIdx + (H & 3u)) & 3u) != 0) return true; }
			else if (d2 > Mid2) { if (((FrameIdx + (H & 1u)) & 1u) != 0) return true; }

			const uint8 Mask = Policy[Idx].SeparationMask;
			return (Mask != 0) && (((FrameIdx + (H & Mask)) & Mask) != 0);
		};

		auto LocalCapFromDensity = [&](float estDensity) -> int32
		{
			if (estDensity >= 6.0f) return FMath::Max(4, FMath::FloorToInt(Params.MaxNeighbors * 0.5f));
			if (estDensity >= 3.0f) return FMath::Max(4, FMath::FloorToInt(Params.MaxNeighbors * 0.75f));
			return Params.MaxNeighbors;
		};

		for (int32 i = 0; i < N; ++i)
		{
			if (ShouldSkip(i)) continue;

			const FVector SelfPos = Pos[i];

			const float EstDensity = Policy[i].EstimatedDensity;
			int32 MaxNbr = LocalCapFromDensity(EstDensity);
			if (MaxNbr <= 0)
			{
				Separation[i].NeighborCount = 0;
				Separation[i].LocalDensity  = 0.f;
				continue;
			}

			FVector Sep = FVector::ZeroVector;
			int32   Count = 0;

			const FMassEntityHandle SelfE = Exec.GetEntity(i);
			auto AccumulateNeighbor = [&](const FEntityData& O) -> bool
			{
				if (O.Entity == SelfE)
				{
					return true;
				}

				const float sumR   = 2 * Params.AgentRadius + Skin;
				const float sumRSq = sumR * sumR;

				const float dx = O.Location.X - SelfPos.X;
				const float dy = O.Location.Y - SelfPos.Y;
				const float ds2 = dx*dx + dy*dy;

				if (ds2 > KINDA_SMALL_NUMBER && ds2 < sumRSq)
				{
					const float d    = FMath::Sqrt(ds2);
					const float invd = 1.f / (d + KINDA_SMALL_NUMBER);
					const float nx = -dx * invd;
					const float ny = -dy * invd;

					const float over = (sumR - d);
					const float strength = 1.f - (d / sumR);

					Sep.X += nx * (over * 8.f + strength * 25.f);
					Sep.Y += ny * (over * 8.f + strength * 25.f);
				}

				++Count;
				return true;
			};

			GridSS->VisitNearby(SelfPos, QueryR, ZHalfHeight, MaxNbr, AccumulateNeighbor);

			const float Density = (Count > 0) ? (float(Count) / QueryAreaM2) : EstDensity;

			Separation[i].Separation    = Sep;
			Separation[i].NeighborCount = Count;
			Separation[i].LocalDensity  = Density;
		}

	}, FMassEntityQuery::EParallelExecutionFlags::Force);

	bool b = false;
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (b) return;
		FSwarmProfilerSharedFragment& Prof         = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		Prof.T_Flocking = (FPlatformTime::Seconds() - T0) * 1000.0;
		b = true;
	});
}

