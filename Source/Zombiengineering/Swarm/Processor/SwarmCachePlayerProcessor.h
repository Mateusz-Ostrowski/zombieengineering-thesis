#pragma once

#include "MassProcessor.h"
#include "MassExecutionContext.h"
#include "DrawDebugHelpers.h"

#include "SwarmCachePlayerProcessor.generated.h"

UCLASS()
class USwarmCachePlayerProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USwarmCachePlayerProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery Query;
};
