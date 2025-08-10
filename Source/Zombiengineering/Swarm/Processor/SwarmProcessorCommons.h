#pragma once
#include "MassExecutionContext.h"
#include "CoreMinimal.h"

namespace SwarmGroups
{
	inline const FName PrePass   = FName(TEXT("Swarm.PrePass"));
	inline const FName Prepare   = FName(TEXT("Swarm.Prepare"));
	inline const FName Sense     = FName(TEXT("Swarm.Sense"));
	inline const FName Path      = FName(TEXT("Swarm.Path"));
	inline const FName Flock     = FName(TEXT("Swarm.Flock"));
	inline const FName Follow    = FName(TEXT("Swarm.Follow"));
	inline const FName Integrate = FName(TEXT("Swarm.Integrate"));
	inline const FName Log       = FName(TEXT("Swarm.Log"));
}

struct FSwarmScopedTimer
{
	double& AccumMs;
	double  StartS;

	explicit FSwarmScopedTimer(double& InAccumMs)
		: AccumMs(InAccumMs), StartS(FPlatformTime::Seconds()) {}

	~FSwarmScopedTimer()
	{
		AccumMs += (FPlatformTime::Seconds() - StartS) * 1000.0;
	}
};

static FORCEINLINE bool ShouldProcessChunkThisFrame(const FMassExecutionContext& Exec, uint32 N = 2)
{
	return (GFrameNumber % N) == (GetTypeHash(Exec.GetEntity(0)) % N);
}