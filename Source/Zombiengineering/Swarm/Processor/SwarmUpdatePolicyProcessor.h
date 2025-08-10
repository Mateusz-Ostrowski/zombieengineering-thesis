#pragma once

#include "MassProcessor.h"
#include "MassExecutionContext.h"

#include "SwarmUpdatePolicyProcessor.generated.h"

UCLASS()
class USwarmUpdatePolicyProcessor : public UMassProcessor
{
	GENERATED_BODY()
public:
	USwarmUpdatePolicyProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery Query;
};
