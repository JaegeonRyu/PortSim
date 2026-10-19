#pragma once
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"

// Opt-in wall-clock diagnostics. Inclusive scopes retain the simulation's time
// steps, sensor periods and interlocks; the profiler never changes their input.
namespace PortSimPerformance
{
    struct FMetric { double Seconds=0; uint64 Calls=0; };
    struct FState
    {
        bool Enabled=FParse::Param(FCommandLine::Get(),TEXT("PortSimProfile"));
        double Start=FPlatformTime::Seconds(), Last=Start, SimulationStart=-1, Duration=0;
        TMap<FName,FMetric> Metrics;
        bool DumpedEngineStats=false;
        FState() { FParse::Value(FCommandLine::Get(),TEXT("PortSimProfileSeconds="),Duration); }
    };
    inline FState& State() { static FState Value; return Value; }
    struct FScope
    {
        FName Name; double Start=0;
        explicit FScope(const TCHAR* Label)
        {
            if(State().Enabled) { Name=FName(Label); Start=FPlatformTime::Seconds(); }
        }
        ~FScope()
        {
            if(Start==0) return;
            auto& Metric=State().Metrics.FindOrAdd(Name);
            Metric.Seconds+=FPlatformTime::Seconds()-Start; ++Metric.Calls;
        }
    };
    inline void Report(double Simulation,int32 Delivered,UWorld* World)
    {
        auto& S=State(); if(!S.Enabled) return;
        const double Now=FPlatformTime::Seconds();
        if(S.SimulationStart<0) { S.Start=S.Last=Now; S.SimulationStart=Simulation; }
        const bool Done=S.Duration>0 && Now-S.Start>=S.Duration;
        if(!Done && Now-S.Last<10.) return;
        S.Last=Now;
        if(!S.DumpedEngineStats && GEngine && FParse::Param(FCommandLine::Get(),TEXT("PortSimProfileEngineStats")))
        { S.DumpedEngineStats=true; GEngine->Exec(World,TEXT("stat dumpframe -ms=1")); }
        UE_LOG(LogTemp,Display,TEXT("PORTSIM_PERF: wall=%.3f processed_sim=%.3f achieved=%.3fx delivered=%d"),
            Now-S.Start,Simulation-S.SimulationStart,(Simulation-S.SimulationStart)/FMath::Max(.001,Now-S.Start),Delivered);
        for(const auto& Pair:S.Metrics)
            UE_LOG(LogTemp,Display,TEXT("PORTSIM_PERF_SCOPE: %s total_ms=%.3f calls=%llu mean_ms=%.6f"),
                *Pair.Key.ToString(),Pair.Value.Seconds*1000,Pair.Value.Calls,
                Pair.Value.Seconds*1000/FMath::Max(uint64(1),Pair.Value.Calls));
        if(Done) FPlatformMisc::RequestExitWithStatus(false,0);
    }
}
