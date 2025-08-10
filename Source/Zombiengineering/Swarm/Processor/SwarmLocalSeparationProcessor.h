#pragma once

#include "MassProcessor.h"
#include "MassExecutionContext.h"

#include "SwarmLocalSeparationProcessor.generated.h"

UCLASS()
class USwarmLocalSeparationProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	USwarmLocalSeparationProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery Query;
};
