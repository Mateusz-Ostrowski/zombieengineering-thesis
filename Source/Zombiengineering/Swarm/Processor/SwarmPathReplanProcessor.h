#pragma once

#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Swarm/Fragment/SwarmTypes.h"
#include "MassExecutionContext.h"
#include "SwarmProcessorCommons.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

#include "SwarmPathReplanProcessor.generated.h"

UCLASS()
class USwarmPathReplanProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	USwarmPathReplanProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery Query;
};
