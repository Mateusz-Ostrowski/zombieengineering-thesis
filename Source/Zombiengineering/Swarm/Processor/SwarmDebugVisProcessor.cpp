#include "SwarmDebugVisProcessor.h"

#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "MassCommonFragments.h"
#include "MassCommonTypes.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Containers/Set.h"
#include "HAL/PlatformTime.h"

#include "Swarm/Fragment/SwarmTypes.h"
#include "Swarm/Grid/SwarmGridSubsystem.h"

#if WITH_EDITOR

#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"

#include "Engine/EngineTypes.h"
#include "DrawDebugHelpers.h"

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarEnabled(
	TEXT("swarm.Debug.Enabled"), 0, TEXT("Master switch 0/1"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarSep(
	TEXT("swarm.Debug.Sep"), 1, TEXT("Draw Separation 0/1"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarQueryDisc(
	TEXT("swarm.Debug.QueryDisc"), 1, TEXT("Draw neighbor query disc 0/1"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarPersistent(
	TEXT("swarm.Debug.Persistent"), 0, TEXT("Persistent line batcher 0/1"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarSample(
	TEXT("swarm.Debug.Sample"), 1, TEXT("Draw only 1 of N entities (>=1)"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarScale(
	TEXT("swarm.Debug.Scale"), 1.f, TEXT("Arrow length multiplier"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarArrowSize(
	TEXT("swarm.Debug.ArrowSize"), 100.f, TEXT("Arrow head size"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarThick(
	TEXT("swarm.Debug.Thick"), 10.f, TEXT("Line thickness"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarLife(
	TEXT("swarm.Debug.Life"), 0.f, TEXT("Lifetime for non-persistent lines (seconds)"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarRiseRate(
	TEXT("swarm.Debug.RiseRate"), 2.f, TEXT("Sticky rise rate (1/s) – how quickly arrows grow"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarFallRate(
	TEXT("swarm.Debug.FallRate"), 2.f, TEXT("Sticky fall rate (1/s) – how slowly arrows shrink"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarHoldFrames(
	TEXT("swarm.Debug.HoldFrames"), 3,
	TEXT("Extra frames to hold non-persistent lines alive to bridge missed frames"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarMinLen(
	TEXT("swarm.Debug.MinLen"), 5.f,
	TEXT("Minimum arrow length in uu before drawing (0 to disable)"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarGridStencil(
	TEXT("swarm.Debug.GridStencil"), 1, TEXT("Draw circular cell stencil around sampled agents 0/1"));
TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarNeighborLines(
	TEXT("swarm.Debug.NeighborLines"), 1, TEXT("Draw lines to neighbors from sampled agents 0/1"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarClosestN(
	TEXT("swarm.Debug.ClosestN"), 50, TEXT("If >0, only draw the N units closest to the reference each frame"));
TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarClosestUseCamera(
	TEXT("swarm.Debug.ClosestUseCamera"), 1, TEXT("1=camera-based selection, 0=player-location selection"));

static TAutoConsoleVariable<float> CVarRayMaxDist(
	TEXT("swarm.Debug.RayMaxDist"), 100000.f, TEXT("Max ray distance from camera when picking"));
static TAutoConsoleVariable<int32> CVarRayDraw(
	TEXT("swarm.Debug.RayDraw"), 0, TEXT("Draw the selection ray/segment 0/1"));

TAutoConsoleVariable<int32> USwarmDebugVisProcessor::CVarPlayerNav(
	TEXT("swarm.Debug.PlayerNav"), 1, TEXT("Draw player nav projection (line+sphere+label) 0/1"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarPlayerNavRadius(
	TEXT("swarm.Debug.PlayerNavRadius"), 25.f, TEXT("Sphere radius for projected nav point"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarPlayerNavXYTol(
	TEXT("swarm.Debug.PlayerNavXYTol"), 5.f, TEXT("On-mesh XY tolerance (cm)"));

TAutoConsoleVariable<float> USwarmDebugVisProcessor::CVarPlayerNavZTol(
	TEXT("swarm.Debug.PlayerNavZTol"), 100.f, TEXT("On-mesh Z tolerance (cm)"));

#endif
USwarmDebugVisProcessor::USwarmDebugVisProcessor() : Query(*this)
{
	bRequiresGameThreadExecution = true;

	ProcessingPhase = EMassProcessingPhase::PostPhysics;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);

	ExecutionFlags = (int32)EProcessorExecutionFlags::Client | (int32)EProcessorExecutionFlags::Standalone;

	bAutoRegisterWithProcessingPhases = true;
}

void USwarmDebugVisProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FSwarmSeparationFragment>(EMassFragmentAccess::ReadOnly);

	Query.AddSharedRequirement<FSwarmMovementParamsFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddSharedRequirement<FPlayerSharedFragment>(EMassFragmentAccess::ReadOnly);
}

static int32 GetEntityStableIndex(const FMassEntityHandle& Handle)
{
	return (int32)Handle.Index;
}

#if WITH_EDITOR

float USwarmDebugVisProcessor::StickyEase(float CurrentShown, float Target, float DT, float RiseRate, float FallRate)
{
	const float Rate  = (Target > CurrentShown) ? FMath::Max(RiseRate, 0.f) : FMath::Max(FallRate, 0.f);
	const float Alpha = 1.f - FMath::Exp(-Rate * DT);
	return CurrentShown + (Target - CurrentShown) * FMath::Clamp(Alpha, 0.f, 1.f);
}

static void BuildCircularOffsets(int32 R, TArray<FIntPoint>& Out)
{
	Out.Reset();
	if (R <= 0) return;
	const int32 R2 = R * R;
	for (int32 dy = -R; dy <= R; ++dy)
	for (int32 dx = -R; dx <= R; ++dx)
	{
		if (dx*dx + dy*dy <= R2)
			Out.Emplace(dx, dy);
	}
}

static FIntPoint CellCoord2D(const FVector& P, float InvCellSize)
{
	return FIntPoint(FMath::FloorToInt(P.X * InvCellSize), FMath::FloorToInt(P.Y * InvCellSize));
}

static void DrawCellWire(UWorld* World, const FIntPoint& C, float CellSize, float Z,
                         const FColor& Color, float Thick, float Life, bool bPersistent)
{
	const FVector Center((C.X + 0.5f) * CellSize, (C.Y + 0.5f) * CellSize, Z);
	const FVector Extent(CellSize * 0.5f, CellSize * 0.5f, 2.f);
	DrawDebugBox(World, Center, Extent, FQuat::Identity, Color, bPersistent, Life, 0, Thick);
}

static bool ResolveActiveCameraView(UWorld* World, FVector& OutLoc, FRotator& OutRot)
{
	if (GEditor && GEditor->bIsSimulatingInEditor && GCurrentLevelEditingViewportClient)
	{
		const FViewportCameraTransform& VT = GCurrentLevelEditingViewportClient->GetViewTransform();
		OutLoc = VT.GetLocation();
		OutRot = VT.GetRotation();
		return true;
	}

	if (World)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->GetPlayerViewPoint(OutLoc, OutRot);
			return true;
		}
	}
	return false;
}
#endif
void USwarmDebugVisProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
#if WITH_EDITOR
	if (CVarEnabled.GetValueOnGameThread() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float DT        = Context.GetDeltaTimeSeconds();
	const float RiseRate  = CVarRiseRate.GetValueOnGameThread();
	const float FallRate  = CVarFallRate.GetValueOnGameThread();
	const float Scale     = CVarScale.GetValueOnGameThread();
	const float ArrowSize = CVarArrowSize.GetValueOnGameThread();
	const float Thick     = CVarThick.GetValueOnGameThread();
	const float LifeCVar  = CVarLife.GetValueOnGameThread();
	const bool  bPersist  = (CVarPersistent.GetValueOnGameThread() != 0);
	const int32 HoldFrames= FMath::Max(0, CVarHoldFrames.GetValueOnGameThread());
	const float MinLen    = FMath::Max(0.f, CVarMinLen.GetValueOnGameThread());

	// If non-persistent & LifeCVar<=0, hold for a couple frames to bridge missed frames (anti-flicker)
	float LifeToUse = LifeCVar;
	if (!bPersist)
	{
		const float Frame = (DT > 0.f) ? DT : (1.f / 60.f);
		LifeToUse = FMath::Max(LifeCVar, HoldFrames * Frame);
	}

	const bool bDrawSep  = (CVarSep.GetValueOnGameThread()   != 0);
	const bool bDrawDisc = (CVarQueryDisc.GetValueOnGameThread() != 0);

	// Closest-N filter (active if >0)
	const int32 ClosestN      = FMath::Max(0, CVarClosestN.GetValueOnGameThread());
	const bool  bUseCameraRef = (CVarClosestUseCamera.GetValueOnGameThread() != 0);

	// Resolve camera view once per frame
	FVector  ViewLoc = FVector::ZeroVector;
	FRotator ViewRot;
	const bool bHaveView = ResolveActiveCameraView(World, ViewLoc, ViewRot);

	// Build camera "selection ray": center of camera towards first blocking hit (or max range if no hit)
	FVector RayStart = ViewLoc;
	FVector RayDir   = ViewRot.Vector();
	const float MaxRayDist = FMath::Max(1000.f, CVarRayMaxDist.GetValueOnGameThread());
	FVector RayEnd   = RayStart + RayDir * MaxRayDist;

	if (bUseCameraRef && bHaveView)
	{
		FHitResult Hit;
		FCollisionQueryParams QParams(SCENE_QUERY_STAT(SwarmDebugVis_RayPick), /*bTraceComplex*/ false);
		QParams.bReturnPhysicalMaterial = false;
		if (World->LineTraceSingleByChannel(Hit, RayStart, RayEnd, ECC_Visibility, QParams))
		{
			RayEnd = Hit.ImpactPoint; // stop at first object hit
		}

		if (CVarRayDraw.GetValueOnGameThread() != 0)
		{
			DrawDebugLine(World, RayStart, RayEnd, FColor::Cyan, /*bPersistentLines*/ false, LifeToUse, 0, FMath::Max(1.f, Thick * 0.5f));
			DrawDebugPoint(World, RayStart, 10.f, FColor::Cyan, false, LifeToUse, 0);
			DrawDebugPoint(World, RayEnd,   10.f, FColor::Yellow, false, LifeToUse, 0);
		}
	}

	// Sampling (disabled if ClosestN is active to avoid hiding selected)
	int32 SampleN = FMath::Max(1, CVarSample.GetValueOnGameThread());
	if (ClosestN > 0)
	{
		SampleN = 1;
	}

	// ---------------- Pass 0: gather closest-N **to the ray ENDPOINT** ----------------
	TSet<FMassEntityHandle> Selected;
	if (ClosestN > 0)
	{
		TArray<TPair<float, FMassEntityHandle>, TInlineAllocator<64>> Best; // (DistSqToRayEnd, entity)
		Best.Reset();

		Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
		{
			const FPlayerSharedFragment& Player = Exec.GetSharedFragment<FPlayerSharedFragment>();
			const TConstArrayView<FTransformFragment> Locs = Exec.GetFragmentView<FTransformFragment>();
			const int32 Num = Exec.GetNumEntities();

			for (int32 i = 0; i < Num; ++i)
			{
				const FVector Origin = Locs[i].GetTransform().GetLocation();
				const FMassEntityHandle E = Exec.GetEntity(i);

				if (bUseCameraRef && bHaveView)
				{
					// Use *distance to the ray end point* (hit point or max range)
					const float DistSq = FVector::DistSquared(Origin, RayEnd);

					if (Best.Num() < ClosestN)
					{
						Best.Emplace(DistSq, E);
					}
					else
					{
						int32 WorstIdx = 0;
						float WorstKey = Best[0].Key;
						for (int32 k = 1; k < Best.Num(); ++k)
						{
							if (Best[k].Key > WorstKey)
							{
								WorstKey = Best[k].Key;
								WorstIdx = k;
							}
						}
						if (DistSq < WorstKey)
						{
							Best[WorstIdx].Key   = DistSq;
							Best[WorstIdx].Value = E;
						}
					}
				}
				else
				{
					// Fallback: closest to player location (2D)
					const float d2 = FVector::DistSquared2D(Origin, Player.PlayerLocation);
					if (Best.Num() < ClosestN)
					{
						Best.Emplace(d2, E);
					}
					else
					{
						int32 WorstIdx = 0;
						float WorstKey = Best[0].Key;
						for (int32 k = 1; k < Best.Num(); ++k)
						{
							if (Best[k].Key > WorstKey)
							{
								WorstKey = Best[k].Key;
								WorstIdx = k;
							}
						}
						if (d2 < WorstKey)
						{
							Best[WorstIdx].Key   = d2;
							Best[WorstIdx].Value = E;
						}
					}
				}
			}
		});

		Selected.Reserve(Best.Num());
		for (const auto& P : Best)
		{
			Selected.Add(P.Value);
		}
	}
	// -------------------------------------------------------------------------

	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		const int32 Num = Exec.GetNumEntities();

		const TConstArrayView<FTransformFragment> Locs   = Exec.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FSwarmSeparationFragment> Separation = Exec.GetFragmentView<FSwarmSeparationFragment>();

		// Shared params for query disc and grid viz
		const FSwarmMovementParamsFragment& Params = Exec.GetSharedFragment<FSwarmMovementParamsFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			const FMassEntityHandle Entity = Exec.GetEntity(i);

			// Closest-N filter (already selected)
			if (ClosestN > 0 && !Selected.Contains(Entity))
			{
				continue;
			}

			// Sampling (stable)
			if ((GetEntityStableIndex(Entity) % SampleN) != 0)
			{
				continue;
			}

			const FVector Origin = Locs[i].GetTransform().GetLocation();

			// Separation magnitude (target)
			const float SepT = Separation[i].Separation.Size();

			// Sticky state
			FArrowVisState& S = StickyState.FindOrAdd(Entity);
			S.SepShown = StickyEase(S.SepShown, SepT, DT, RiseRate, FallRate);

			// Draw separation arrow (smoothed length + floor)
			if (bDrawSep && !Separation[i].Separation.IsNearlyZero())
			{
				const FVector Dir = Separation[i].Separation.GetSafeNormal();
				const float   Len = FMath::Max(S.SepShown * Scale, MinLen);
				DrawArrow(World, Origin, Dir * Len, FColor::Magenta, 1.f, ArrowSize, Thick, LifeToUse, bPersist);
			}

			// Optional neighbor query disc (shared params)
			if (bDrawDisc && Params.NeighborRadius > 0.f)
			{
				DrawQueryDisc(World, Origin, Params.NeighborRadius, FColor::Silver, Thick, LifeToUse, bPersist);
			}

			if (CVarGridStencil.GetValueOnGameThread() != 0 ||
				CVarNeighborLines.GetValueOnGameThread() != 0)
			{
				if (USwarmGridSubsystem* GridSS = World->GetSubsystem<USwarmGridSubsystem>())
				{
					const FAgentSpatialHashGrid& Grid = GridSS->GetGrid();
					const float CellSize = Grid.GetCellSize();
					if (CellSize > KINDA_SMALL_NUMBER)
					{
						const float InvCellSize  = 1.f / CellSize;
						const float QueryR       = Params.NeighborRadius;
						const int32 R            = FMath::Max(1, FMath::CeilToInt(QueryR / CellSize));
						const FIntPoint Center   = CellCoord2D(Origin, InvCellSize);

						if (CVarGridStencil.GetValueOnGameThread() != 0)
						{
							thread_local TArray<FIntPoint> Offsets;
							BuildCircularOffsets(R, Offsets);

							DrawCellWire(World, Center, CellSize, Origin.Z, FColor::Orange, Thick, LifeToUse, bPersist);
							
							for (const FIntPoint& D : Offsets)
							{
								if (D.X == 0 && D.Y == 0) continue;
								const FIntPoint C = FIntPoint(Center.X + D.X, Center.Y + D.Y);
								DrawCellWire(World, C, CellSize, Origin.Z, FColor::Silver, Thick, LifeToUse, bPersist);
							}
						}

						if (CVarNeighborLines.GetValueOnGameThread() != 0)
						{
							const float ZHalf = 120.f;
							const int32 MaxN  = 64;
							Grid.VisitNearby(Origin, QueryR, ZHalf, MaxN, [&](const FEntityData& E)
							{
								if (E.Entity == Exec.GetEntity(i)) return true;

								DrawDebugLine(World, Origin, E.Location, FColor::Purple, bPersist, LifeToUse, 0, FMath::Max(1.f, Thick * 0.5f));
								return true;
							});
						}
					}
				}
			}
		}
	});

	if (CVarPlayerNav.GetValueOnGameThread() != 0)
	{
		bool bDrew = false;

		Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
		{
			if (bDrew) return;

			const FPlayerSharedFragment& Player = Exec.GetSharedFragment<FPlayerSharedFragment>();

			const FVector RawLoc = Player.PlayerLocation;
			const FVector Proj   = Player.PlayerNavLocation;
			const bool   bOnMesh = Player.bIsOnNavMesh;

			const float XYTol = CVarPlayerNavXYTol.GetValueOnGameThread();
			const float ZTol  = CVarPlayerNavZTol.GetValueOnGameThread();
			const float XYDistSq = FVector::DistSquared2D(RawLoc, Proj);
			const float ZDist    = FMath::Abs(RawLoc.Z - Proj.Z);
			const bool  bFallback = (!bOnMesh) && (XYDistSq > FMath::Square(XYTol) || ZDist > ZTol);

			const FColor Col = bOnMesh ? FColor::Green : (bFallback ? FColor::Yellow : FColor::Red);

			constexpr bool  bPersistPlayer = false;
			constexpr float LifePlayer     = 0.0f;

			const float ThickPlayer = CVarThick.GetValueOnGameThread();
			const float Rad         = FMath::Max(1.f, CVarPlayerNavRadius.GetValueOnGameThread());

			DrawDebugLine  (World, RawLoc, Proj, Col, bPersistPlayer, LifePlayer, 0, ThickPlayer);
			DrawDebugSphere(World, Proj, Rad, 16, Col, bPersistPlayer, LifePlayer, 0, ThickPlayer);
			DrawDebugString(World, Proj + FVector(0,0,30.f),
				bOnMesh ? TEXT("NavProj: ON") : (bFallback ? TEXT("NavProj: FALLBACK") : TEXT("NavProj: OFF")),
				nullptr, Col, LifePlayer, false);

			bDrew = true;
		});
	}
	
	if (StickyState.Num() > 0 && (FPlatformTime::Cycles() & 127) == 0)
	{
		TArray<FMassEntityHandle> KeysToRemove;
		KeysToRemove.Reserve(StickyState.Num());
		for (const TPair<FMassEntityHandle, FArrowVisState>& Pair : StickyState)
		{
			if (!EntityManager.IsEntityValid(Pair.Key))
			{
				KeysToRemove.Add(Pair.Key);
			}
		}
		for (const FMassEntityHandle& Key : KeysToRemove)
		{
			StickyState.Remove(Key);
		}
	}
#endif
}

#if WITH_EDITOR

void USwarmDebugVisProcessor::DrawArrow(
	UWorld* World, const FVector& Origin, const FVector& V,
	const FColor& Color, float Scale, float ArrowSize, float Thick, float Life, bool bPersistent)
{
	if (!World) return;

	const FVector Tip = Origin + V * Scale;
	const uint8 DepthPriority = 0;
	DrawDebugDirectionalArrow(
		World,
		Origin,
		Tip,
		ArrowSize,
		Color,
		bPersistent,
		Life,
		DepthPriority,
		Thick
	);
}

void USwarmDebugVisProcessor::DrawQueryDisc(
	UWorld* World, const FVector& Origin, float Radius,
	const FColor& Color, float Thick, float Life, bool bPersistent)
{
	if (!World || Radius <= 0.f) return;

	const int32 Segments = 48;
	DrawDebugCircle(
		World,
		Origin,
		Radius,
		Segments,
		Color,
		bPersistent,
		Life,
		0,
		Thick,
		FVector(1,0,0),
		FVector(0,1,0),
		false
	);
}

#endif
