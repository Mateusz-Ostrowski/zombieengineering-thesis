#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"

#include "SwarmBuildSpatialGridProcessor.generated.h"

UCLASS()
class USwarmBuildSpatialGridProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USwarmBuildSpatialGridProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery Query;
};
