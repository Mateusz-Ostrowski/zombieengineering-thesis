#pragma once
#include "MassProcessor.h"
#include "SwarmIntegrateProcessor.generated.h"

UCLASS()
class USwarmIntegrateProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USwarmIntegrateProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager&, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery IntegrateQuery;
	uint32 HCache = 0;
};
