#pragma once
#include "CoreMinimal.h"

class AQuayCrane;
class FTerminalCheckpoint;
struct FTerminalReplayLog
{
    FName Category;
    ELogVerbosity::Type Verbosity;
    FString Message;
};

// Checkpoint transactions exclude Chaos history. Supported terminal cargo can
// instead advance real physics forward in fixed steps, without checkpointing.
// External bodies and unsupported physics settings retain normal world ticking.
class FTerminalReplay
{
public:
    bool Tick(AQuayCrane& Owner,double WallSeconds);
    void Invalidate();
    const TCHAR* Mode() const { return State; }
    double BufferedSeconds() const;
    double DisplayedSeconds() const { return DisplayedSteps/60.; }
    uint64 Generation() const { return Revision; }
    float AchievedRate() const { return ActualRate; }
    static bool IsPredicting();
    static bool IsAdvancingPhysics();
    static void Log(FName Category,ELogVerbosity::Type Verbosity,FString Message);
private:
    friend class FTerminalReplayCheckpointTest;
    bool AdvanceLivePhysics(AQuayCrane& Owner);
    TArray<TSharedPtr<FTerminalCheckpoint>> Future;
    TArray<int32> FutureSteps;
    TArray<TArray<FTerminalReplayLog>> FutureLogs;
    TSharedPtr<FTerminalCheckpoint> Current;
    const TCHAR* State=TEXT("live");
    uint64 Revision=0,DisplayedSteps=0;
    double Credit=0;
    bool bBoundary=false;
    double SampleWall=0,SampleSimulation=0;
    float ActualRate=0;
};

#define PORTSIM_SIM_LOG(Category,Verbosity,Format,...) \
    do { if(!Category.IsSuppressed(ELogVerbosity::Verbosity)) \
        FTerminalReplay::Log(Category.GetCategoryName(),ELogVerbosity::Verbosity,FString::Printf(Format,##__VA_ARGS__)); } while(0)
