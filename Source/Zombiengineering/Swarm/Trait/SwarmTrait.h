#pragma once

#include "MassEntityTraitBase.h"

#include "SwarmTrait.generated.h"

UCLASS()
class ZOMBIENGINEERING_API USwarmTrait : public UMassEntityTraitBase
{
	GENERATED_BODY()
protected:
	virtual void BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const override;
};
