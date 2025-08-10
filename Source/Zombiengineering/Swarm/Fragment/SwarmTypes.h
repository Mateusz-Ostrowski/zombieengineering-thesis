#pragma once

#include "MassEntityTypes.h"
#include "CoreMinimal.h"

#include "SwarmTypes.generated.h"

USTRUCT()
struct FSwarmAgentFragment : public FMassFragment
{
	GENERATED_BODY()
	
	FVector Velocity               = FVector::ZeroVector;
	FVector LastProjectedLocation  = FVector::ZeroVector;
	float   LaneSign               = 1.f;
	float   LaneMag                = 1.f;

	uint8   bYielding : 1 = 0;
	float   YieldTimeRemaining = 0.0f;
};

USTRUCT()
struct FSwarmPathStateFragment : public FMassFragment
{
	GENERATED_BODY()

	TSharedPtr<const TArray<FVector>> PointsRef;
	int32   Index = 0;
	FVector LastGoal = FVector::ZeroVector;
	float PathAge         = 0.f;
	float RepathCooldown  = 0.f;
	float NoLOSTime       = 0.f;
	uint8 bHasPath : 1 = 0;

	FORCEINLINE int32 NumPoints() const { return PointsRef ? PointsRef->Num() : 0; }
	FORCEINLINE const FVector& Point(int32 i) const { return (*PointsRef)[i]; }
};

USTRUCT()
struct FSwarmLOSFragment : public FMassFragment
{
	GENERATED_BODY()

	uint8  bHasLOS : 1 = 0;
	float  TimeSinceRefresh = 0.f;
};

USTRUCT()
struct FSwarmSeparationFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Separation    = FVector::ZeroVector;
	FVector PathDir       = FVector::ZeroVector;
	
	float   PathWeight    = 0.f;
	int32   NeighborCount = 0;
	float   LocalDensity = 0.f;      
};

USTRUCT()
struct FSwarmMovementParamsFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	float MaxSpeed                = 330.f;
	float SeparationWeight        = 450.f;
	float PathFollowWeight        = 3.0f;

	float NeighborRadius          = 80.f;
	float AgentRadius             = 55.f;
	int32 MaxNeighbors            = 4;

	float WaypointAcceptanceRadius = 180.f;
	float EndOfPathRepathRadius    = 700.f;
	float LOSHeightOffset          = 60.f;
	float DirectChaseRange         = 1400.f;
	float PathSpreadMaxOffset      = 120.f;
	float PathSpreadMinDistance    = 600.f;
	float PathSpreadMaxDistance    = 3000.f;

	int32 RepathsPerFrameBudget    = 256;
	int32 LOSChecksPerFrameBudget  = 64;
	float LOSRefreshSeconds        = 0.35f;
};

USTRUCT()
struct FSwarmProgressFragment : public FMassFragment
{
	GENERATED_BODY()
	FVector LastPos2D        = FVector::ZeroVector;
	float   DistanceMoved2D  = 0.f;
	float   SinceProgressSec = 0.f;
	uint8   bLikelyStuck : 1 = 0;
};

USTRUCT()
struct FSwarmProfilerSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	double T_BuildGrid   = 0.0;
	double T_PlayerCache = 0.0;
	double T_UpdatePolicy = 0.0;
	double T_Perception  = 0.0;
	double T_PathReplan  = 0.0;
	double T_Flocking    = 0.0;
	double T_PathFollow  = 0.0;
	double T_Integrate   = 0.0;

	uint8  bPrintedHeader : 1 = 0;

	int32 RepathsUsed     = 0;
	int32 LOSChecksUsed   = 0;

	int32  DirectChaseCount = 0;
	double AvgPathAgeAccum  = 0.0;
	int32  AvgPathAgeNum    = 0;
};

USTRUCT()
struct FPlayerSharedFragment : public FMassSharedFragment
{
	GENERATED_BODY()

	FVector   PlayerLocation    = FVector::ZeroVector;
	FVector2D PlayerLocation2D  = FVector2D::ZeroVector;
	FVector   PlayerNavLocation = FVector::ZeroVector;

	uint8     bIsOnNavMesh : 1 = 0;

	double    LastUpdateSeconds = -1.0;
};

USTRUCT()
struct FSwarmTargetSenseFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector TargetLocation = FVector::ZeroVector;

	uint8 bLOS : 1 = 0;
	uint8 bLOSUpdated : 1 = 0;
};

USTRUCT()
struct FSwarmBudgetStampFragment : public FMassFragment
{
	GENERATED_BODY()
	uint8 bDidLOSRefresh   : 1 = 0;
	uint8 bDidReplan       : 1 = 0;
};

USTRUCT()
struct FSwarmPathWindowFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector P0 = FVector::ZeroVector;
	FVector P1 = FVector::ZeroVector;
	FVector P2 = FVector::ZeroVector;

	FVector Tangent2D = FVector::ZeroVector;
	float   Curvature = 0.f;
	uint8   bValid : 1 = 0;
};

USTRUCT()
struct FSwarmUpdatePolicyFragment : public FMassFragment
{
	GENERATED_BODY()

	float DistToPlayer2D_Sq = FLT_MAX;
	float EstimatedDensity   = 0.f;
	float CooldownScale      = 1.f;

	uint8 SeparationMask  = 0;
	uint8 FollowMask = 0;
	uint8 SenseMask  = 0;
};
