#include "SwarmGridSubsystem.h"

void USwarmGridSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Grid = MakeUnique<FAgentSpatialHashGrid>(CellSize);
}
