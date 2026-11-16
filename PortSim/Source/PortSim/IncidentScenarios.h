#pragma once
#include "CoreMinimal.h"

class FJsonObject;
class AActor;
class APortContainerActor;
namespace PortIncident
{
    inline bool AGVExtension(const FString& A)
    {return A==TEXT("localization_loss")||A==TEXT("fms_delay")||A==TEXT("fms_disconnect")||A==TEXT("road_block");}
    inline bool CraneExtension(const FString& A,bool RMG)
    {return A==TEXT("equipment_failure")||A==TEXT("fail_release")||(!RMG&&(A==TEXT("fail_hoist")||A==TEXT("agv_unavailable")))||(RMG&&(A==TEXT("slot_occupied")||A==TEXT("height_mismatch")));}
    inline bool Extension(const FString& A){return AGVExtension(A)||CraneExtension(A,false)||CraneExtension(A,true);}
}
struct FPortIncidentRoadClosure
{
    FBox Bounds=FBox(ForceInit);
    bool Active=true,Responded=false;
    int32 Affected=0;
    TWeakObjectPtr<AActor> Marker;
};
struct FPortIncidentStep
{
    TSharedPtr<FJsonObject> Command;
    FString State=TEXT("pending"),Message;
    double Delay=0,Duration=0,Timeout=1800,AppliedAt=-1,RecoveredAt=-1;
    double MeasureAppliedAt=-1,MeasureClearedAt=-1,MeasureResumedAt=-1,MeasureCompletedAt=-1;
    TSet<FString> AffectedActors,AffectedCargo;
    int32 ResumeStage=INDEX_NONE,ResumeCompleted=0;
    int32 CompletedBefore=0;
    bool Observed=false;
    bool ExpectShip=false;
    TWeakObjectPtr<APortContainerActor> Cargo;
};
struct FPortIncidentRun
{
    FString ID,Name;
    double StartedAt=0;
    bool Cancelled=false;
    TArray<FPortIncidentStep> Steps;
};
