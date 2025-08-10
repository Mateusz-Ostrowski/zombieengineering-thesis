#include "SwarmCsvLogProcessor.h"

#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "MassCommonTypes.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "Swarm/Fragment/SwarmTypes.h"
#include "SwarmProcessorCommons.h"
#include "RenderingThread.h"

#include "Misc/App.h"
#include "RHI.h"
#include "RHIStats.h"
#include "RHIUtilities.h"
#include "HAL/IConsoleManager.h"

#include <cfloat>

DEFINE_LOG_CATEGORY(LogSwarmCsv);

extern RENDERCORE_API uint32 GRenderThreadTime;
extern RENDERCORE_API uint32 GGameThreadTime;
extern RENDERCORE_API uint32 GSwapBufferTime;

static uint64 GFrameCountForNewStats = 0;

static double Accum_CPU_ProcPctNorm = 0.0;
static double Min_CPU_ProcPctNorm   = DBL_MAX;
static double Max_CPU_ProcPctNorm   = 0.0;

static double Accum_CPU_IdlePctNorm = 0.0;
static double Min_CPU_IdlePctNorm   = DBL_MAX;
static double Max_CPU_IdlePctNorm   = 0.0;

static double Accum_MemUsedPhysMB   = 0.0;
static double Min_MemUsedPhysMB     = DBL_MAX;
static double Max_MemUsedPhysMB     = 0.0;

static double Accum_MemUsedVirtMB   = 0.0;
static double Min_MemUsedVirtMB     = DBL_MAX;
static double Max_MemUsedVirtMB     = 0.0;

static double Accum_GPU_FrameMS     = 0.0;
static double Min_GPU_FrameMS       = DBL_MAX;
static double Max_GPU_FrameMS       = 0.0;

static void GetMemoryStatsMB(double& UsedPhysMB, double& PeakUsedPhysMB, double& UsedVirtMB, double& PeakUsedVirtMB)
{
	const FPlatformMemoryStats S = FPlatformMemory::GetStats();
	UsedPhysMB     = S.UsedPhysical     / (1024.0 * 1024.0);
	PeakUsedPhysMB = S.PeakUsedPhysical / (1024.0 * 1024.0);
	UsedVirtMB     = S.UsedVirtual      / (1024.0 * 1024.0);
	PeakUsedVirtMB = S.PeakUsedVirtual  / (1024.0 * 1024.0);
}

static bool GetProcessCPUMetrics(float& OutProcPctNorm, float& OutIdlePctNorm)
{
	float ProcUsage = 0.0f;
	float IdleUsage = 0.0f;

	const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
	if (FPlatformProcess::GetPerFrameProcessorUsage(Pid, ProcUsage, IdleUsage))
	{
		const int32 NumCores = FPlatformMisc::NumberOfCoresIncludingHyperthreads();
		if (NumCores > 0)
		{
			OutProcPctNorm = (ProcUsage * 100.0f) / NumCores;
			OutIdlePctNorm = (IdleUsage * 100.0f) / NumCores;
		}
		else
		{
			OutProcPctNorm = ProcUsage * 100.0f;
			OutIdlePctNorm = IdleUsage * 100.0f;
		}
		return true;
	}
	OutProcPctNorm = -1.0f;
	OutIdlePctNorm = -1.0f;
	return false;
}

static void EnsureGPUStatsOn()
{
	static bool bOnce = false;
	if (!bOnce)
	{
		if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GPUStatsEnabled")))
		{
			C->Set(1, ECVF_SetByCode);
		}
		bOnce = true;
	}
}

// ---------------------------------------------

USwarmCsvLogProcessor::USwarmCsvLogProcessor()
	: Query(*this)
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteInGroup = SwarmGroups::Log;

	ExecutionFlags = (uint8)(
		EProcessorExecutionFlags::Standalone |
		EProcessorExecutionFlags::Server |
		EProcessorExecutionFlags::Client);

	RegisterQuery(Query);

	StartTime = FPlatformTime::Seconds();

	EnsureGPUStatsOn();
}

void USwarmCsvLogProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddSharedRequirement<FSwarmProfilerSharedFragment>(EMassFragmentAccess::ReadWrite);
}

void USwarmCsvLogProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	uint64 EntitiesThisFrame = 0;
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& CtxCount)
	{
		EntitiesThisFrame += (uint64)CtxCount.GetNumEntities();
	});
	LatestEntityCount = EntitiesThisFrame;
	MaxEntityCount    = FMath::Max(MaxEntityCount, EntitiesThisFrame);
	AccumEntityCount += EntitiesThisFrame;

	const double ExecStartS = FPlatformTime::Seconds();
	bool bDidLog = false;

	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Exec)
	{
		if (bDidLog)
			return;

		const double Elapsed = FPlatformTime::Seconds() - StartTime;

		const double Dt      = FMath::Max(Exec.GetDeltaTimeSeconds(), KINDA_SMALL_NUMBER);
		const double InstFPS = 1.0 / Dt;
		const double Alpha   = 0.1;
		SmoothedFPS = (SmoothedFPS <= 0.0) ? InstFPS : FMath::Lerp(SmoothedFPS, InstFPS, Alpha);

		FSwarmProfilerSharedFragment& P = Exec.GetMutableSharedFragment<FSwarmProfilerSharedFragment>();

		const double T_Total =
			P.T_BuildGrid + P.T_UpdatePolicy + P.T_Perception + P.T_PathReplan +
			P.T_Flocking + P.T_PathFollow + P.T_Integrate + P.T_PlayerCache;

		double UsedPhysMB=0, PeakUsedPhysMB=0, UsedVirtMB=0, PeakUsedVirtMB=0;
		GetMemoryStatsMB(UsedPhysMB, PeakUsedPhysMB, UsedVirtMB, PeakUsedVirtMB);

		float CpuProcPctNorm = -1.0f;
		float CpuIdlePctNorm = -1.0f;
		GetProcessCPUMetrics(CpuProcPctNorm, CpuIdlePctNorm);

		double RawGPUFrameMS = -1.0;
		if (FApp::CanEverRender())
		{
			const uint32 GPUCycles = RHIGetGPUFrameCycles();
			RawGPUFrameMS = FPlatformTime::ToMilliseconds(GPUCycles);

		}

		if (!P.bPrintedHeader)
		{
			UE_LOG(LogSwarmCsv, Warning, TEXT("Time,"
				"T_BuildGrid,T_UpdatePolicy,T_Perception,T_PathReplan,T_Flocking,T_PathFollow,T_Integrate,"
				"T_PlayerCache,"
				"T_Total,"
				"AvgPathAge,DirectChaseCount,RepathsUsed,LOSChecksUsed,FPS,"
				"Mem_UsedPhysMB,Mem_PeakPhysMB,Mem_UsedVirtMB,Mem_PeakVirtMB,"
				"CPU_ProcPctNorm,CPU_IdlePctNorm,GPU_FrameMS"));
			P.bPrintedHeader = true;
		}

		const double AvgPathAge = (P.AvgPathAgeNum > 0) ? (P.AvgPathAgeAccum / P.AvgPathAgeNum) : 0.0;

		UE_LOG(LogSwarmCsv, Warning, TEXT("%.3f,"
			"%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
			"%.3f,%.3f,"
			"%.3f,%d,%d,%d,%.3f,"
			"%.3f,%.3f,%.3f,%.3f,"
			"%.3f,%.3f,%.3f"),
			Elapsed,
			P.T_BuildGrid, P.T_UpdatePolicy, P.T_Perception, P.T_PathReplan, P.T_Flocking, P.T_PathFollow, P.T_Integrate,
			P.T_PlayerCache, T_Total,
			AvgPathAge, P.DirectChaseCount, P.RepathsUsed, P.LOSChecksUsed, SmoothedFPS,
			UsedPhysMB, PeakUsedPhysMB, UsedVirtMB, PeakUsedVirtMB,
			(double)CpuProcPctNorm, (double)CpuIdlePctNorm, RawGPUFrameMS);

		FrameCount++;

		Accum_T_BuildGrid    += P.T_BuildGrid;
		Accum_T_UpdatePolicy += P.T_UpdatePolicy;
		Accum_T_Perception   += P.T_Perception;
		Accum_T_PathReplan   += P.T_PathReplan;
		Accum_T_Flocking     += P.T_Flocking;
		Accum_T_PathFollow   += P.T_PathFollow;
		Accum_T_Integrate    += P.T_Integrate;

		Accum_T_PlayerCache  += P.T_PlayerCache;

		Accum_T_Total += T_Total;

		Accum_AvgPathAge     += AvgPathAge;
		Accum_FPS            += SmoothedFPS;

		UpdateMinMax(Min_T_BuildGrid,    Max_T_BuildGrid,    P.T_BuildGrid);
		UpdateMinMax(Min_T_UpdatePolicy, Max_T_UpdatePolicy, P.T_UpdatePolicy);
		UpdateMinMax(Min_T_Perception,   Max_T_Perception,   P.T_Perception);
		UpdateMinMax(Min_T_PathReplan,   Max_T_PathReplan,   P.T_PathReplan);
		UpdateMinMax(Min_T_Flocking,     Max_T_Flocking,     P.T_Flocking);
		UpdateMinMax(Min_T_PathFollow,   Max_T_PathFollow,   P.T_PathFollow);
		UpdateMinMax(Min_T_Integrate,    Max_T_Integrate,    P.T_Integrate);

		UpdateMinMax(Min_T_PlayerCache,  Max_T_PlayerCache,  P.T_PlayerCache);

		UpdateMinMax(Min_T_Total,    Max_T_Total,    T_Total);

		UpdateMinMax(Min_AvgPathAge,     Max_AvgPathAge,     AvgPathAge);
		UpdateMinMax(Min_FPS,            Max_FPS,            SmoothedFPS);

		GFrameCountForNewStats++;

		Accum_CPU_ProcPctNorm += (double)CpuProcPctNorm;
		Min_CPU_ProcPctNorm    = FMath::Min(Min_CPU_ProcPctNorm, (double)CpuProcPctNorm);
		Max_CPU_ProcPctNorm    = FMath::Max(Max_CPU_ProcPctNorm, (double)CpuProcPctNorm);

		Accum_CPU_IdlePctNorm += (double)CpuIdlePctNorm;
		Min_CPU_IdlePctNorm    = FMath::Min(Min_CPU_IdlePctNorm, (double)CpuIdlePctNorm);
		Max_CPU_IdlePctNorm    = FMath::Max(Max_CPU_IdlePctNorm, (double)CpuIdlePctNorm);

		Accum_MemUsedPhysMB += UsedPhysMB;
		Min_MemUsedPhysMB    = FMath::Min(Min_MemUsedPhysMB, UsedPhysMB);
		Max_MemUsedPhysMB    = FMath::Max(Max_MemUsedPhysMB, UsedPhysMB);

		Accum_MemUsedVirtMB += UsedVirtMB;
		Min_MemUsedVirtMB    = FMath::Min(Min_MemUsedVirtMB, UsedVirtMB);
		Max_MemUsedVirtMB    = FMath::Max(Max_MemUsedVirtMB, UsedVirtMB);

		if (RawGPUFrameMS >= 0.0)
		{
			Accum_GPU_FrameMS += RawGPUFrameMS;
			Min_GPU_FrameMS    = FMath::Min(Min_GPU_FrameMS, RawGPUFrameMS);
			Max_GPU_FrameMS    = FMath::Max(Max_GPU_FrameMS, RawGPUFrameMS);
		}

		P.T_BuildGrid = P.T_UpdatePolicy = P.T_Perception = P.T_PathReplan =
			P.T_Flocking = P.T_PathFollow = P.T_Integrate = 0.0;

		P.T_PlayerCache = 0.0;

		P.RepathsUsed = P.LOSChecksUsed = 0;
		P.DirectChaseCount = 0;
		P.AvgPathAgeAccum = 0.0;
		P.AvgPathAgeNum   = 0;

		bDidLog = true;
	});
}

void USwarmCsvLogProcessor::BeginDestroy()
{
	Super::BeginDestroy();

	if (FrameCount == 0)
		return;

	UE_LOG(LogSwarmCsv, Warning, TEXT("==== Swarm CSV Summary ===="));
	UE_LOG(LogSwarmCsv, Warning, TEXT("Time: %.3f"), FPlatformTime::Seconds() - StartTime);
	PrintStat(TEXT("T_BuildGrid"),    Accum_T_BuildGrid,    FrameCount, Min_T_BuildGrid,    Max_T_BuildGrid);
	PrintStat(TEXT("T_UpdatePolicy"), Accum_T_UpdatePolicy, FrameCount, Min_T_UpdatePolicy, Max_T_UpdatePolicy);
	PrintStat(TEXT("T_Perception"),   Accum_T_Perception,   FrameCount, Min_T_Perception,   Max_T_Perception);
	PrintStat(TEXT("T_PathReplan"),   Accum_T_PathReplan,   FrameCount, Min_T_PathReplan,   Max_T_PathReplan);
	PrintStat(TEXT("T_Flocking"),     Accum_T_Flocking,     FrameCount, Min_T_Flocking,     Max_T_Flocking);
	PrintStat(TEXT("T_PathFollow"),   Accum_T_PathFollow,   FrameCount, Min_T_PathFollow,   Max_T_PathFollow);
	PrintStat(TEXT("T_Integrate"),    Accum_T_Integrate,    FrameCount, Min_T_Integrate,    Max_T_Integrate);

	PrintStat(TEXT("T_PlayerCache"),  Accum_T_PlayerCache,  FrameCount, Min_T_PlayerCache,  Max_T_PlayerCache);

	PrintStat(TEXT("T_Total"),    Accum_T_Total,    FrameCount, Min_T_Total,    Max_T_Total);

	PrintStat(TEXT("AvgPathAge"),     Accum_AvgPathAge,     FrameCount, Min_AvgPathAge,     Max_AvgPathAge);
	PrintStat(TEXT("FPS"),            Accum_FPS,            FrameCount, Min_FPS,            Max_FPS);

	if (GFrameCountForNewStats > 0)
	{
		PrintStat(TEXT("CPU_ProcPctNorm"), Accum_CPU_ProcPctNorm, GFrameCountForNewStats, Min_CPU_ProcPctNorm, Max_CPU_ProcPctNorm);
		PrintStat(TEXT("CPU_IdlePctNorm"), Accum_CPU_IdlePctNorm, GFrameCountForNewStats, Min_CPU_IdlePctNorm, Max_CPU_IdlePctNorm);

		PrintStat(TEXT("Mem_UsedPhysMB"),  Accum_MemUsedPhysMB,   GFrameCountForNewStats, Min_MemUsedPhysMB,   Max_MemUsedPhysMB);
		PrintStat(TEXT("Mem_UsedVirtMB"),  Accum_MemUsedVirtMB,   GFrameCountForNewStats, Min_MemUsedVirtMB,   Max_MemUsedVirtMB);

		PrintStat(TEXT("GPU_FrameMS"),     Accum_GPU_FrameMS,     GFrameCountForNewStats, Min_GPU_FrameMS,     Max_GPU_FrameMS);
	}
	
	UE_LOG(LogSwarmCsv, Warning, TEXT("Entities  : %llu"), (unsigned long long)MaxEntityCount);
}

void USwarmCsvLogProcessor::UpdateMinMax(double& MinVal, double& MaxVal, double Sample)
{
	MinVal = FMath::Min(MinVal, Sample);
	MaxVal = FMath::Max(MaxVal, Sample);
}

void USwarmCsvLogProcessor::PrintStat(const TCHAR* Name, double Accum, uint64 Count, double Min, double Max)
{
	const double Avg = (Count > 0) ? (Accum / static_cast<double>(Count)) : 0.0;
	UE_LOG(LogSwarmCsv, Warning, TEXT("%s -> Avg: %.3f, Min: %.3f, Max: %.3f"), Name, Avg, Min, Max);
}
