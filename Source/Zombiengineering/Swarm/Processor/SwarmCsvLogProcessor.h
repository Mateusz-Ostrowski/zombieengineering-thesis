#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "SwarmCsvLogProcessor.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSwarmCsv, Log, All);

UCLASS()
class USwarmCsvLogProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	USwarmCsvLogProcessor();

	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
	virtual void BeginDestroy() override;

private:
	static void UpdateMinMax(double& MinVal, double& MaxVal, double Sample);
	static void PrintStat(const TCHAR* Name, double Accum, uint64 Count, double Min, double Max);

private:
	FMassEntityQuery Query;

	double StartTime   = 0.0;
	double SmoothedFPS = 0.0;

	uint64 FrameCount        = 0;
	uint64 LatestEntityCount = 0;
	uint64 MaxEntityCount    = 0;
	uint64 AccumEntityCount  = 0;

	double Accum_T_BuildGrid    = 0.0;
	double Accum_T_UpdatePolicy = 0.0;
	double Accum_T_Perception   = 0.0;
	double Accum_T_PathReplan   = 0.0;
	double Accum_T_Flocking     = 0.0;
	double Accum_T_PathFollow   = 0.0;
	double Accum_T_Integrate    = 0.0;
	double Accum_T_PlayerCache  = 0.0;
	double Accum_T_Total        = 0.0;
	double Accum_AvgPathAge     = 0.0;
	double Accum_FPS            = 0.0;

	double Min_T_BuildGrid    = TNumericLimits<double>::Max(); double Max_T_BuildGrid    = 0.0;
	double Min_T_UpdatePolicy = TNumericLimits<double>::Max(); double Max_T_UpdatePolicy = 0.0;
	double Min_T_Perception   = TNumericLimits<double>::Max(); double Max_T_Perception   = 0.0;
	double Min_T_PathReplan   = TNumericLimits<double>::Max(); double Max_T_PathReplan   = 0.0;
	double Min_T_Flocking     = TNumericLimits<double>::Max(); double Max_T_Flocking     = 0.0;
	double Min_T_PathFollow   = TNumericLimits<double>::Max(); double Max_T_PathFollow   = 0.0;
	double Min_T_Integrate    = TNumericLimits<double>::Max(); double Max_T_Integrate    = 0.0;

	double Min_T_PlayerCache  = TNumericLimits<double>::Max(); double Max_T_PlayerCache  = 0.0;
	
	double Min_T_Total    = TNumericLimits<double>::Max(); double Max_T_Total    = 0.0;

	double Min_AvgPathAge     = TNumericLimits<double>::Max(); double Max_AvgPathAge     = 0.0;
	double Min_FPS            = TNumericLimits<double>::Max(); double Max_FPS            = 0.0;
};
