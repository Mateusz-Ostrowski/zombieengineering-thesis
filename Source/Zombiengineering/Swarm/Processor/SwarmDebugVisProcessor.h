#pragma once

#include "CoreMinimal.h"

#include "MassProcessor.h"
#include "MassEntityQuery.h"
#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#endif
#include "SwarmDebugVisProcessor.generated.h"

UCLASS()
class USwarmDebugVisProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USwarmDebugVisProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>&) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery Query;

	struct FArrowVisState
	{
		float SepShown = 0.f;
	};
	TMap<FMassEntityHandle, FArrowVisState> StickyState;

#if WITH_EDITOR

	static void DrawArrow(UWorld* World, const FVector& Origin, const FVector& V,
		const FColor& Color, float Scale, float ArrowSize, float Thick, float Life, bool bPersistent);

	static void DrawQueryDisc(UWorld* World, const FVector& Origin, float Radius,
		const FColor& Color, float Thick, float Life, bool bPersistent);

	static float StickyEase(float CurrentShown, float Target, float DT, float RiseRate, float FallRate);

	static TAutoConsoleVariable<int32>  CVarEnabled;
	static TAutoConsoleVariable<int32>  CVarSep;
	static TAutoConsoleVariable<int32>  CVarQueryDisc;
	static TAutoConsoleVariable<int32>  CVarPersistent;
	static TAutoConsoleVariable<int32>  CVarSample;
	static TAutoConsoleVariable<float>  CVarScale;
	static TAutoConsoleVariable<float>  CVarArrowSize;
	static TAutoConsoleVariable<float>  CVarThick;
	static TAutoConsoleVariable<float>  CVarLife;
	static TAutoConsoleVariable<float>  CVarRiseRate;
	static TAutoConsoleVariable<float>  CVarFallRate;
	static TAutoConsoleVariable<int32>  CVarHoldFrames;
	static TAutoConsoleVariable<float>  CVarMinLen;
	static TAutoConsoleVariable<int32>  CVarGridStencil;
	static TAutoConsoleVariable<int32>  CVarNeighborLines;
	static TAutoConsoleVariable<int32>  CVarClosestN;
	static TAutoConsoleVariable<int32>  CVarClosestUseCamera;
	static TAutoConsoleVariable<int32>  CVarPlayerNav;
	static TAutoConsoleVariable<float>  CVarPlayerNavRadius;
	static TAutoConsoleVariable<float>  CVarPlayerNavXYTol;
	static TAutoConsoleVariable<float>  CVarPlayerNavZTol;
#endif

};
