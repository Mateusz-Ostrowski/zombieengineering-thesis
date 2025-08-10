#include "SwarmTrait.h"

#include "MassActorSubsystem.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"
#include "MassNavigationFragments.h"
#include "Swarm/Fragment/SwarmTypes.h"
#include "MassEntityUtils.h"

void USwarmTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FSwarmUpdatePolicyFragment>();
	
	BuildContext.AddFragment<FTransformFragment>();
	BuildContext.AddFragment<FMassMoveTargetFragment>();
	
	BuildContext.AddFragment<FSwarmAgentFragment>();
	BuildContext.AddFragment<FSwarmPathStateFragment>();
	BuildContext.AddFragment<FSwarmLOSFragment>();
	BuildContext.AddFragment<FSwarmSeparationFragment>();

	BuildContext.AddFragment<FSwarmTargetSenseFragment>();
	BuildContext.AddFragment<FSwarmBudgetStampFragment>();
	BuildContext.AddFragment<FSwarmPathWindowFragment>(); 
	BuildContext.AddFragment<FSwarmProgressFragment>();
	
	BuildContext.AddFragment<FMassActorFragment>();
	BuildContext.AddFragment<FAgentRadiusFragment>();
	
	FMassEntityManager& EntityManager = UE::Mass::Utils::GetEntityManagerChecked(World);

	{
		FSwarmMovementParamsFragment Params{};
		const FSharedStruct ParamsShared =
			EntityManager.GetOrCreateSharedFragment<FSwarmMovementParamsFragment>(FConstStructView::Make(Params), Params);
		BuildContext.AddSharedFragment(ParamsShared);
	}

	{
		FSwarmProfilerSharedFragment ProfInit{};
		const FSharedStruct ProfShared =
			EntityManager.GetOrCreateSharedFragment<FSwarmProfilerSharedFragment>(FConstStructView::Make(ProfInit), ProfInit);
		BuildContext.AddSharedFragment(ProfShared);
	}
	
	{
		FPlayerSharedFragment Player{};
		const FSharedStruct PlayerShared =
			EntityManager.GetOrCreateSharedFragment<FPlayerSharedFragment>(FConstStructView::Make(Player), Player);
		BuildContext.AddSharedFragment(PlayerShared);
	}

}
