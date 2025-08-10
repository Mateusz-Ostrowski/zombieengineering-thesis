#include "SwarmIntegrateProcessor.h"

#include "HAL/PlatformTime.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "MassNavigationFragments.h"
#include "SwarmProcessorCommons.h"
#include "Swarm/Fragment/SwarmTypes.h"

USwarmIntegrateProcessor::USwarmIntegrateProcessor()
	: IntegrateQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionOrder.ExecuteInGroup = SwarmGroups::Integrate;
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteBefore.Add(SwarmGroups::Log);

	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Standalone |
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Client);

	RegisterQuery(IntegrateQuery);
}

void USwarmIntegrateProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	IntegrateQuery.AddRequirement<FSwarmAgentFragment>(EMassFragmentAccess::ReadWrite);
	IntegrateQuery.AddRequirement<FSwarmSeparationFragment>(EMassFragmentAccess::ReadWrite);
	IntegrateQuery.AddRequirement<FSwarmPathStateFragment>(EMassFragmentAccess::ReadWrite);
	IntegrateQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	IntegrateQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	IntegrateQuery.AddRequirement<FSwarmTargetSenseFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddRequirement<FSwarmPathWindowFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddRequirement<FSwarmProgressFragment>(EMassFragmentAccess::ReadWrite);

	IntegrateQuery.AddSharedRequirement<FSwarmMovementParamsFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadOnly);
	IntegrateQuery.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void USwarmIntegrateProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UWorld* World = Context.GetWorld();
	if (!World) return;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const uint32 FrameIdx = (uint32)(World->TimeSeconds * 60.0f);

	const double T0 = FPlatformTime::Seconds();
	
	IntegrateQuery.ParallelForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		const FSwarmMovementParamsFragment& Params = Exec.GetSharedFragment<FSwarmMovementParamsFragment>();
		const FPlayerSharedFragment& Player        = Exec.GetSharedFragment<FPlayerSharedFragment>();
		FSwarmProfilerSharedFragment& Prof         = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();

		const int32 N = Exec.GetNumEntities();
		if (N == 0) return;

		auto Agents     = Exec.GetMutableFragmentView<FSwarmAgentFragment>();
		auto Separation = Exec.GetMutableFragmentView<FSwarmSeparationFragment>();
		auto Paths      = Exec.GetMutableFragmentView<FSwarmPathStateFragment>();
		auto Xforms     = Exec.GetMutableFragmentView<FTransformFragment>();
		auto Move       = Exec.GetMutableFragmentView<FMassMoveTargetFragment>();
		auto Sense      = Exec.GetFragmentView<FSwarmTargetSenseFragment>();
		auto PWin       = Exec.GetFragmentView<FSwarmPathWindowFragment>();
		auto Prog       = Exec.GetMutableFragmentView<FSwarmProgressFragment>();

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

		auto Freeze = [&](int i, const FVector& selfPos, const FVector& fwd2D)
		{
			Separation[i].Separation = FVector::ZeroVector;
			Separation[i].PathDir    = FVector::ZeroVector;
			Separation[i].PathWeight = 0.f;

			if (!Agents[i].Velocity.IsNearlyZero())
				Agents[i].Velocity = FVector::ZeroVector;

			Move[i].Center         = selfPos;
			Move[i].Forward        = fwd2D;
			Move[i].DistanceToGoal = 0.f;
		};

		for (int32 i = 0; i < N; ++i)
		{
			FTransform& T = Xforms[i].GetMutableTransform();
			const FVector selfPos = T.GetLocation();
			const FVector2D self2D(selfPos);

			Prog[i].SinceProgressSec += Dt;
			if (Prog[i].SinceProgressSec >= 0.25f)
			{
				const float d2 = FVector2D::DistSquared(self2D, FVector2D(Prog[i].LastPos2D));
				if (d2 >= 400.f)
				{
					Prog[i].LastPos2D = FVector(self2D, 0.f);
					Prog[i].DistanceMoved2D = 0.f;
					Prog[i].bLikelyStuck = 0;
				}
				else
				{
					Prog[i].bLikelyStuck = 1;
				}
				Prog[i].SinceProgressSec = 0.f;
			}

			const FVector fwd2D = T.GetRotation().GetForwardVector().GetSafeNormal2D();
			if (!IsPathFresh(i, selfPos))
			{
				Freeze(i, selfPos, fwd2D);
				continue;
			}

			const float ZTol              = 120.f;
			const float EnterMul          = 2.0f;
			const float ExitMul           = 2.4f;
			const float DensityFracToStop = 0.6f;
			const float MinStopSpeed      = 10.f;
			const float SepPressureTh     = Params.MaxSpeed * 0.25f;
			const float MinHoldSec        = 0.40f;

			const float dzToPlayer = FMath::Abs(selfPos.Z - Player.PlayerLocation.Z);
			const float r          = FMath::Max(100.f, Params.AgentRadius);
			const bool  nearEnter  = (FVector2D::DistSquared(self2D, Player.PlayerLocation2D) <= FMath::Square(EnterMul * r)) && (dzToPlayer <= ZTol);
			const bool  nearExit   = (FVector2D::DistSquared(self2D,  Player.PlayerLocation2D) <= FMath::Square(ExitMul  * r)) && (dzToPlayer <= ZTol);

			const int32 maxNeigh = FMath::Max(1, Params.MaxNeighbors);
			const bool  dense    = (Separation[i].NeighborCount >= FMath::CeilToInt(DensityFracToStop * maxNeigh));
			const float speed2D  = Agents[i].Velocity.Size2D();
			const float sepMag   = Separation[i].Separation.Size2D();
			const bool  slow     = (speed2D <= MinStopSpeed);
			const bool  pressured= (sepMag >= SepPressureTh) && (speed2D <= Params.MaxSpeed * 0.2f);

			const bool enterYield = nearEnter && dense && (slow || pressured);

			Agents[i].YieldTimeRemaining = FMath::Max(0.f, Agents[i].YieldTimeRemaining - Dt);

			if (!Agents[i].bYielding && enterYield)
			{
				Agents[i].bYielding = true;
				Agents[i].YieldTimeRemaining = MinHoldSec;
			}
			else if (Agents[i].bYielding)
			{
				const bool crowdRelaxed = (Separation[i].NeighborCount < FMath::CeilToInt(0.4f * maxNeigh));
				const bool playerFar    = !nearExit;
				if (Agents[i].YieldTimeRemaining <= 0.f && (crowdRelaxed || playerFar))
				{
					Agents[i].bYielding = false;
				}
			}

			if (Agents[i].bYielding)
			{
				Separation[i].Separation = FVector::ZeroVector;
				Separation[i].PathDir    = FVector::ZeroVector;
				Separation[i].PathWeight = 0.f;
				if (!Agents[i].Velocity.IsNearlyZero())
					Agents[i].Velocity = FVector::ZeroVector;

				FVector desiredPos = selfPos;
				if (UNLIKELY(NavSys))
				{
					FNavLocation out;
					static const FVector Small (100.f, 100.f, 200.f);
					static const FVector Medium(400.f, 400.f, 400.f);
					static const FVector Large (1200.f,1200.f, 800.f);
					if (NavSys->ProjectPointToNavigation(desiredPos, out, Small)  ||
						NavSys->ProjectPointToNavigation(desiredPos, out, Medium) ||
						NavSys->ProjectPointToNavigation(desiredPos, out, Large))
					{
						desiredPos = out.Location;
						Agents[i].LastProjectedLocation = out.Location;
					}
				}

				Move[i].Center         = desiredPos;
				Move[i].Forward        = fwd2D;
				Move[i].DistanceToGoal = 0.f;
				continue;
			}

			const float neighFrac = FMath::Clamp(
				float(Separation[i].NeighborCount) / float(FMath::Max(1, Params.MaxNeighbors)), 0.f, 1.f);
			const float localDensityFrac = (Separation[i].LocalDensity > 0.f)
				? FMath::Clamp(Separation[i].LocalDensity / 2.5f, 0.f, 1.f)
				: neighFrac;

			int32 period = 1;
			if      (localDensityFrac >= 0.85f) period = 4;
			else if (localDensityFrac >= 0.60f) period = 2;

			const uint32 h = GetTypeHash(Exec.GetEntity(i));
			const bool bDecimate = (period > 1) && (((FrameIdx + (h & (period - 1))) % period) != 0);

			if (bDecimate)
			{
				Agents[i].Velocity *= 0.90f;
				Move[i].Center         = selfPos;
				Move[i].Forward        = fwd2D;
				Move[i].DistanceToGoal = 0.f;
				continue;
			}

			Separation[i].PathWeight *= (1.f - 0.5f * localDensityFrac);

			FVector desiredVel =  Separation[i].Separation * Params.SeparationWeight;
			desiredVel        += (Separation[i].PathDir * Params.MaxSpeed) * Separation[i].PathWeight;

			float curvSpeedScale = 1.f;
			if (PWin[i].bValid)
			{
				const float k = 120.f;
				curvSpeedScale = FMath::Clamp(1.f / (1.f + k * PWin[i].Curvature), 0.55f, 1.f);
			}
			const float densitySpeedScale = FMath::Lerp(1.f, 0.6f, localDensityFrac);

			const float maxSpeedThisFrame = Params.MaxSpeed * curvSpeedScale * densitySpeedScale;
			const float turnRateLimitDeg  = FMath::Lerp(720.f, 180.f, localDensityFrac);

			const FVector desiredVel2D(desiredVel.X, desiredVel.Y, 0.f);

			if (desiredVel2D.Size() * Dt <= 0.5f)
			{
				Agents[i].Velocity *= 0.90f;
				Move[i].Center         = selfPos;
				Move[i].Forward        = fwd2D;
				Move[i].DistanceToGoal = 0.f;
				continue;
			}
			
			const FVector currV(Agents[i].Velocity.X, Agents[i].Velocity.Y, 0.f);
			const FVector blendV = FMath::VInterpTo(currV, desiredVel2D, Dt, 6.0f);
			const FVector newVel = blendV.GetClampedToMaxSize(maxSpeedThisFrame);
			if (!newVel.Equals(Agents[i].Velocity))
				Agents[i].Velocity = newVel;
			
			const FVector target2D = Agents[i].Velocity.IsNearlyZero() ? fwd2D : Agents[i].Velocity.GetSafeNormal2D();
			const float   turnCapRad = FMath::DegreesToRadians(turnRateLimitDeg) * Dt;
			const float   cosCap     = FMath::Cos(turnCapRad);

			float dot = FVector::DotProduct(fwd2D, target2D);
			dot = FMath::Clamp(dot, -1.f, 1.f);
			if (dot < cosCap)
			{
				const float ang = FMath::Acos(dot);
				const float t   = FMath::Min(1.f, turnCapRad / FMath::Max(KINDA_SMALL_NUMBER, ang));
				const FVector newFwd = (fwd2D * (1.f - t) + target2D * t).GetSafeNormal2D();
				T.SetRotation(FRotationMatrix::MakeFromX(newFwd).ToQuat());
			}

			FVector desiredPos = selfPos + Agents[i].Velocity * Dt;
			
			const FVector lastProj = Agents[i].LastProjectedLocation;
			const float distXYSq   = FVector::DistSquared2D(selfPos, lastProj);
			const float dZ         = FMath::Abs(selfPos.Z - lastProj.Z);

			const float speed2DNow = Agents[i].Velocity.Size2D();
			const float xySlack    = FMath::Clamp(120.f - 0.5f * speed2DNow, 60.f, 120.f);
			const float zSlack     = FMath::Clamp( 20.f - 0.05f * speed2DNow, 10.f, 20.f);

			const bool needReproject = (distXYSq > FMath::Square(xySlack)) || (dZ > zSlack);
			const bool haveBudget    = ((FrameIdx + (HCache++ & 0x3)) % 4) == 0;

			if (needReproject && haveBudget)
			{
				FNavLocation out;
				static const FVector Small (100.f, 100.f, 200.f);
				static const FVector Medium(400.f, 400.f, 400.f);
				static const FVector Large (1200.f,1200.f, 800.f);

				if (NavSys->ProjectPointToNavigation(desiredPos, out, Small)  ||
					NavSys->ProjectPointToNavigation(desiredPos, out, Medium) ||
					NavSys->ProjectPointToNavigation(desiredPos, out, Large))
				{
					desiredPos = out.Location;
					Agents[i].LastProjectedLocation = out.Location;
				}
			}

			Move[i].Center         = desiredPos;
			Move[i].Forward        = Agents[i].Velocity.IsNearlyZero() ? fwd2D : Agents[i].Velocity.GetSafeNormal2D();
			Move[i].DistanceToGoal = Agents[i].Velocity.Size() * Dt;
		}

	}, FMassEntityQuery::EParallelExecutionFlags::Force);

	bool b = false;
	IntegrateQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (b) return;
		FSwarmProfilerSharedFragment& Prof         = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();
		Prof.T_Integrate = (FPlatformTime::Seconds() - T0) * 1000.0;
		b = true;
	});
}
