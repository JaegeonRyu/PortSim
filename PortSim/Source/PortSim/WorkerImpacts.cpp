#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortWorkingCrane.h"
#include "Dom/JsonObject.h"

void APortSiteLogistics::UpdateWorkerImpacts()
{
    // Current dependencies, not every planned destination or nearby machine.
    TMap<FString,TSharedPtr<FJsonObject>> Previous,Next;
    auto Key=[](const FJsonObject& E)
    { return E.GetStringField(TEXT("cause_actor"))+TEXT("|")+E.GetStringField(TEXT("to_actor"))+TEXT("|")+
        E.GetStringField(TEXT("from_actor"))+TEXT("|")+E.GetStringField(TEXT("reason")); };
    for(const auto& E:WorkerImpacts) Previous.Add(Key(*E),E);
    TMap<FString,AActor*> Actors;
    for(const auto& V:Vehicles) if(IsValid(V))Actors.Add(V->GetName(),V);
    for(const auto& C:Equipment) if(IsValid(C))Actors.Add(C->GetName(),C);
    auto Add=[&](AActor* Cause,AActor* Target,AActor* Blocker,const TCHAR* Reason)
    {
        auto E=MakeShared<FJsonObject>();
        E->SetStringField(TEXT("cause_actor"),GetNameSafe(Cause));
        E->SetStringField(TEXT("to_actor"),GetNameSafe(Target));
        E->SetStringField(TEXT("from_actor"),GetNameSafe(Blocker));
        E->SetStringField(TEXT("reason"),Reason);
        const FString K=Key(*E);
        if(Next.Contains(K))return;
        const auto* Old=Previous.Find(K);
        const double Since=Old?(*Old)->GetNumberField(TEXT("started_at_seconds")):SimulationTime;
        E->SetNumberField(TEXT("started_at_seconds"),Since);
        E->SetNumberField(TEXT("duration_seconds"),FMath::Max(0.,SimulationTime-Since));
        Next.Add(K,E);
        if(!Old)
        {
            RecordRecoveryEvent(TEXT("worker_impact_started"),Cause,Blocker,Target);
            RecoveryEvents.Last()->SetStringField(TEXT("reason"),Reason);
        }
    };
    for(int32 Root=0;Root<Vehicles.Num();++Root)
    {
        auto* Cause=Vehicles[Root].Get();
        if(!IsValid(Cause) || !Cause->Emergency.Active)continue;
        TSet<int32> Affected; Affected.Add(Root);
        Add(Cause,Cause,Cause,TEXT("worker_intrusion"));
        for(int32 Pass=0;Pass<Vehicles.Num();++Pass)
        {
            bool Changed=false;
            for(int32 Lane=0;Lane<Vehicles.Num();++Lane)
            {
                if(Affected.Contains(Lane))continue;
                const auto* V=Vehicles[Lane].Get();if(!IsValid(V))continue;
                const int32* Block=RoadBlockers.Find(V->VehicleID);
                int32 BlockedBy=INDEX_NONE;bool ByLiDAR=false;
                for(int32 Other:Affected)
                {
                    const int32 ID=Vehicles[Other]->VehicleID;
                    const bool FMS=V->Sensors.bFMSHold && Block && *Block==ID;
                    const bool LiDAR=V->Sensors.bControlledStop && V->Sensors.LiDARBlockingVehicleID==ID;
                    if(!FMS && !LiDAR)continue;
                    BlockedBy=Other;ByLiDAR=LiDAR;break;
                }
                if(BlockedBy!=INDEX_NONE)
                { Add(Cause,Vehicles[Lane],Vehicles[BlockedBy],ByLiDAR?TEXT("lidar_queue"):TEXT("fms_queue"));Affected.Add(Lane);Changed=true; }
            }
            if(!Changed)break;
        }
        for(int32 Lane:Affected)
        {
            if(!Jobs.IsValidIndex(Lane))continue;
            const auto& J=Jobs[Lane];
            const int32 Crane=YardCraneCount+J.STS;
            if(J.STS>=0 && STSOwners.IsValidIndex(J.STS) && STSOwners[J.STS]==Lane &&
                Equipment.IsValidIndex(Crane) && Equipment[Crane]->IsWaitingForAGV())
                Add(Cause,Equipment[Crane],Vehicles[Lane],TEXT("sts_handover_wait"));
            if(J.bRMGReserved && !J.bYardReleased && Equipment.IsValidIndex(J.RMG) && !Equipment[J.RMG]->IsBusy())
                Add(Cause,Equipment[J.RMG],Vehicles[Lane],TEXT("rmg_reservation_wait"));
        }
    }
    for(const auto& Pair:Previous) if(!Next.Contains(Pair.Key))
    {
        const auto& E=Pair.Value;
        RecordRecoveryEvent(TEXT("worker_impact_cleared"),Actors.FindRef(E->GetStringField(TEXT("cause_actor"))),
            Actors.FindRef(E->GetStringField(TEXT("from_actor"))),Actors.FindRef(E->GetStringField(TEXT("to_actor"))));
        RecoveryEvents.Last()->SetStringField(TEXT("reason"),E->GetStringField(TEXT("reason")));
        RecoveryEvents.Last()->SetNumberField(TEXT("duration_seconds"),FMath::Max(0.,SimulationTime-E->GetNumberField(TEXT("started_at_seconds"))));
    }
    WorkerImpacts.Reset();for(const auto& Pair:Next)WorkerImpacts.Add(Pair.Value);
}
