#pragma once

#include "MassProcessor.h"
#include "MassExecutionContext.h"

#include "SwarmPerceptionProcessor.generated.h"

UCLASS()
class USwarmPerceptionProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	USwarmPerceptionProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery Query;

	uint32 LastLOSResetFrame = 0;
};
