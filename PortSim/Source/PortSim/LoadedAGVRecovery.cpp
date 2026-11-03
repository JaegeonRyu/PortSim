#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortSupportVehicle.h"
#include "PortContainerActor.h"
#include "PortWorkingCrane.h"
#include "AGVRoadNetwork.h"
#include "AGVApronRoads.h"
#include "AGVTrafficGeometry.h"
#include "AGVReference.h"
#include "TerminalLayout.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"

namespace SiteLogistics
{
    TArray<FVector> RoundRoadCorners(const FVector&,const TArray<FVector>&,TFunctionRef<bool(FVector,FVector)>);
    bool RouteIntersectsContainer(const TArray<FVector>&,const FVector&,const TArray<FSiteYardSlot>&,int32);
}

bool APortSiteLogistics::IsLoadedRecoveryVehicle(int32 V) const
{
    return LoadedAGVRecoveries.ContainsByPredicate([V](const auto& R){return R.Failed==V || R.Replacement==V;});
}

bool APortSiteLogistics::ValidateLoadedAGVRecoveries(FString& Error) const
{
    TSet<int32> Assigned; TSet<const APortContainerActor*> Boxes; TSet<const APortSupportVehicle*> Stackers;
    for(const auto& R:LoadedAGVRecoveries)
    {
        if(!Vehicles.IsValidIndex(R.Failed) || !Vehicles.IsValidIndex(R.Replacement) ||
            R.Failed==R.Replacement || Assigned.Contains(R.Failed) || Assigned.Contains(R.Replacement) ||
            !R.Stacker.IsValid() || Stackers.Contains(R.Stacker.Get()) || R.Cargo.IsEmpty() ||
            R.Cargo.Num()!=R.DeckTransforms.Num() || Jobs[R.Replacement].Stage!=FSiteTransfer::ToRecovery)
        {Error=TEXT("Invalid/duplicate loaded AGV recovery assignment");return false;}
        Assigned.Add(R.Failed);Assigned.Add(R.Replacement);Stackers.Add(R.Stacker.Get());
        for(int32 I=0;I<R.Cargo.Num();++I)
        {
            const auto* C=R.Cargo[I].Get();
            const bool OnNew=I<R.Box, OnRS=I==R.Box && R.Phase>=2 && R.Phase<=5;
            const AActor* Expected=OnNew?static_cast<AActor*>(Vehicles[R.Replacement]):OnRS?
                static_cast<AActor*>(R.Stacker.Get()):static_cast<AActor*>(Vehicles[R.Failed]);
            if(!C || Boxes.Contains(C) || C->GetAttachParentActor()!=Expected || C->GetBody()->IsSimulatingPhysics() ||
                C->LocationOwner!=(OnRS?ECargoOwner::ReachStacker:ECargoOwner::AGV))
            {Error=TEXT("Loaded recovery lost single cargo ownership");return false;}
            if(!OnRS && !C->GetActorTransform().Equals(R.DeckTransforms[I]*Expected->GetActorTransform(),1.f))
            {Error=TEXT("Loaded recovery deck alignment mismatch");return false;}
            Boxes.Add(C);
        }
    }
    return true;
}

bool APortSiteLogistics::CanRecoverLoadedAGV(int32 V,FString& Error) const
{
    if(!Jobs.IsValidIndex(V) || !Vehicles[V]->bDriveFault)
    { Error=TEXT("적재 AGV의 주행 고장이 필요합니다."); return false; }
    if(IsLoadedRecoveryVehicle(V)) { Error=TEXT("이미 R/S 복구 작업 중입니다."); return false; }
    const auto& J=Jobs[V];
    if(Vehicles[V]->Emergency.Active || Vehicles[V]->Speed>.1f || !J.Actor.IsValid() ||
        J.Actor->LocationOwner!=ECargoOwner::AGV || J.Actor->GetAttachParentActor()!=Vehicles[V])
    { Error=TEXT("AGV가 정지하고 화물을 소유한 상태에서 배정하세요."); return false; }
    for(const auto& C:Equipment) if(C->IsBusy() && C->GetHandoverVehicle()==Vehicles[V])
    { Error=TEXT("크레인 인계가 진행 중입니다. 인계 종료 후 배정하세요."); return false; }
    if(J.Stage!=3 && J.Stage!=9 && !(J.ExportIndex!=INDEX_NONE && (J.Stage==5 || J.Stage==6)))
    { Error=TEXT("화물 운송 도로 또는 야드 진입 대기 단계에서 배정하세요."); return false; }
    return true;
}

// Directed road network plus local recovery access. This is a blockout route,
// not an OEM reach-stacker dynamics/load-chart model.
bool APortSiteLogistics::PlanLoadedRecoveryRoad(FVector Start,FVector Goal,FVector Axis,int32 Depot,TArray<FVector>& Route) const
{
    FAGVRoadNetwork N;
    auto P=[](float X,float Y){return FVector(TerminalLayout::SiteCmX(X),Y,0);};
    const float South=TerminalLayout::AGVSouthReturnY,North=TerminalLayout::AGVNorthInboundY;
    for(float X:{12500.f,15300.f,TerminalLayout::AGVInlandInboundX}) N.Add(P(X,South),P(X,North));
    for(float X:{14000.f,16700.f,TerminalLayout::AGVInlandReturnX}) N.Add(P(X,North),P(X,South));
    for(float Y:{TerminalLayout::AGVSouthInboundY,TerminalLayout::AGVCentralInboundY,TerminalLayout::AGVNorthInboundY})
        N.Add(P(12500,Y),P(TerminalLayout::AGVInlandReturnX,Y));
    for(float Y:{South,TerminalLayout::AGVCentralReturnY,TerminalLayout::AGVNorthReturnY})
        N.Add(P(TerminalLayout::AGVInlandReturnX,Y),P(12500,Y));
    AGVApronRoads::Add(N);
    N.Add(P(12500,North),P(12500,TerminalLayout::AGVStandbyNorthY));
    N.Add(P(14000,TerminalLayout::AGVStandbyNorthY),P(14000,North));
    if(Depot!=INDEX_NONE)
    {
        const FVector Park=FleetPark(Depot);
        const float Y=TerminalLayout::AGVStandbyExitY(Depot);
        N.Add(Park,FVector(Park.X,Y,0)); N.Add(P(4800,Y),P(14000,Y));
    }
    else
    {
        // Support equipment apron connects to the inland service road at its
        // north end; never cut across occupied yard stacks.
        const FVector Access(Start.X,North,0);
        N.Add(Start,Access,true); N.Add(Access,P(TerminalLayout::AGVInlandReturnX,North),true);
    }
    const FVector Approach=Goal-Axis*2200;
    const auto Roads=N.Roads;
    bool Connected=false;
    for(const auto& Road:Roads)
    {
        const FVector D=Road.B-Road.A;
        const double StartT=FMath::Clamp(FVector::DotProduct(Start-Road.A,D)/D.SizeSquared(),0.,1.);
        const FVector StartOn=Road.A+D*StartT;
        if(FVector::Dist2D(Start,StartOn)<3500) N.Add(Start,StartOn);
        const double T=FMath::Clamp(FVector::DotProduct(Approach-Road.A,D)/D.SizeSquared(),0.,1.);
        const FVector On=Road.A+D*T;
        if(FVector::Dist2D(On,Approach)>3500) continue;
        N.Add(On,Approach,true); Connected=true;
    }
    if(!Connected) return false;
    N.Add(Approach,Goal);
    TArray<FVector> Raw;
    auto ObstacleCost=[&](FVector A,FVector B)
    {
        double Cost=0;
        for(const auto& V:Vehicles) if(V->bDriveFault && !V->GetActorLocation().Equals(Start,10) &&
            AGVTrafficGeometry::SweptChassisIntersects(A,B,V->GetActorLocation(),V->GetActorRightVector(),730,210,50)) Cost+=1.e9;
        return Cost;
    };
    if(!N.Find(Start,Goal,Raw,ObstacleCost)) return false;
    Route=SiteLogistics::RoundRoadCorners(Start,Raw,[](FVector,FVector){return true;});
    FVector Previous=Start;
    for(const FVector& Point:Route)
    {if(ObstacleCost(Previous,Point)>0) return false;Previous=Point;}
    if(Depot!=INDEX_NONE) for(const FVector& Point:Route)
        if(!TerminalLayout::AGVEnvelopeInside(Point.X-760,Point.X+760,Point.Y-760,Point.Y+760)) return false;
    return !Route.IsEmpty() && AGVTrafficGeometry::RoundedRouteHasNoSharpTurns(Start,Route) &&
        !SiteLogistics::RouteIntersectsContainer(Route,Start,Yard,INDEX_NONE);
}

bool APortSiteLogistics::RequestLoadedAGVRecovery(int32 Failed,FString& Error)
{
    if(!CanRecoverLoadedAGV(Failed,Error)) return false;
    auto* Old=Vehicles[Failed].Get();
    FLoadedAGVRecovery R; R.Failed=Failed;
    double Mass=0;
    for(const auto& C:ShipContainers) if(C->GetAttachParentActor()==Old && C->LocationOwner==ECargoOwner::AGV)
    {
        if(C->MassKg>45000) {Error=TEXT("R/S 가정 인양 한도 45t 초과입니다.");return false;}
        R.Cargo.Add(C); R.DeckTransforms.Add(C->GetActorTransform().GetRelativeTransform(Old->GetActorTransform())); Mass+=C->MassKg;
    }
    if(R.Cargo.IsEmpty() || Mass>AGVReference::MaximumPayloadKg)
    {Error=TEXT("인계할 화물 또는 대체 AGV 하중을 확인하세요.");return false;}
    // Parallel receiving bay, with the stacker between the two stopped decks.
    const FVector Axis=Old->GetActorRightVector();
    for(int32 Side:{1,-1})
    {
        R.Target=Old->GetActorLocation()+Old->GetActorForwardVector()*float(Side*900);
        R.StackerTarget=(Old->GetActorLocation()+R.Target)*.5f;
        const FVector Extent=Old->GetActorForwardVector().GetAbs()*210+Axis.GetAbs()*730;
        if(!TerminalLayout::AGVEnvelopeInside(R.Target.X-Extent.X,R.Target.X+Extent.X,R.Target.Y-Extent.Y,R.Target.Y+Extent.Y)) continue;
        for(int32 V=0;V<Jobs.Num();++V)
        {
            if(V==Failed || Jobs[V].Stage || !Vehicles[V]->CanAcceptWork() || IsLoadedRecoveryVehicle(V)) continue;
            TArray<FVector> Route;
            if(!PlanLoadedRecoveryRoad(Vehicles[V]->GetActorLocation(),R.Target,Axis,V,Route)) continue;
            for(TActorIterator<APortSupportVehicle> It(GetWorld());It;++It)
            {
                if(It->EquipmentType!=EPortSupportType::ReachStacker ||
                    LoadedAGVRecoveries.ContainsByPredicate([&](const auto& Other){return Other.Stacker.Get()==*It;})) continue;
                if(!PlanLoadedRecoveryRoad(It->GetActorLocation(),R.StackerTarget,Axis,INDEX_NONE,R.StackerRoute)) continue;
                R.Replacement=V; R.Stacker=*It; R.StackerHome=It->GetActorTransform(); R.Status=TEXT("대체 AGV · R/S 현장 이동");
                auto& J=Jobs[V]; J=FSiteTransfer(); J.Stage=FSiteTransfer::ToRecovery; J.Route=MoveTemp(Route);
                J.RouteStart=Vehicles[V]->GetActorLocation(); J.bRouteStartValid=true;
                LoadedAGVRecoveries.Add(R);
                RecordRecoveryEvent(TEXT("loaded_agv_dispatched"),Old,Old,Vehicles[V],Jobs[Failed].Actor.Get());
                return true;
            }
        }
    }
    Error=TEXT("가용 AGV·R/S 또는 안전한 현장 접근 경로가 없습니다. 장비와 작업 공간 확보 후 다시 배정하세요.");
    return false;
}

void APortSiteLogistics::AdvanceLoadedAGVRecoveries(float Dt)
{
    for(int32 I=LoadedAGVRecoveries.Num()-1;I>=0;--I)
    {
        auto& R=LoadedAGVRecoveries[I]; auto* Old=Vehicles[R.Failed].Get(); auto* New=Vehicles[R.Replacement].Get();
        auto* RS=R.Stacker.Get(); auto& Arrival=Jobs[R.Replacement];
        Old->SetFMSHold(true);
        if(!RS || New->bDriveFault || New->Emergency.Active || Old->Emergency.Active)
        {New->SetFMSHold(true);R.Status=TEXT("복구 장비 고장 · 화물 유지 · 수리 대기");continue;}
        if(R.Phase==0)
        {
            R.Status=TEXT("대체 AGV · R/S 현장 이동");
            if(Arrival.Route.IsValidIndex(Arrival.Waypoint))
            {
                New->SetFMSHold(false);
                if(MoveVehicle(New,Arrival.Route[Arrival.Waypoint],Dt)) ++Arrival.Waypoint;
            }
            if(R.StackerRoute.IsValidIndex(R.Waypoint))
            {
                const FVector P=RS->GetActorLocation(),Goal=R.StackerRoute[R.Waypoint];
                const FVector Next=FMath::VInterpConstantTo(P,Goal,Dt,200.f);
                bool Clear=true;
                for(const auto& V:TrafficVehicles) if(IsValid(V) &&
                    AGVTrafficGeometry::SweptChassisIntersects(P,Next,V->GetActorLocation(),V->GetActorRightVector(),450,170,50)) {Clear=false;break;}
                if(Clear)
                {
                    RS->SetActorLocation(Next);
                    if(!Next.Equals(P,.01)) RS->SetActorRotation((Next-P).Rotation());
                    if(Next.Equals(Goal,5)) ++R.Waypoint;
                }
                else R.Status=TEXT("R/S 진입 공간 확보 대기");
            }
            if(Arrival.Waypoint<Arrival.Route.Num() || R.Waypoint<R.StackerRoute.Num() || New->Speed>.1f) continue;
            New->SetFMSHold(true); R.Phase=1; R.HoldSeconds=0;
        }
        New->SetFMSHold(true);
        if(Old->Speed>.1f || New->Speed>.1f || !New->GetActorLocation().Equals(R.Target,6))
        {R.Status=TEXT("정지·안착 위치 확인 대기");continue;}
        const int32 BoxIndex=FMath::Min(R.Box,R.Cargo.Num()-1);
        auto* C=R.Cargo[BoxIndex].Get();
        if(!C) {R.Status=TEXT("화물 참조 오류 · 복구 정지");continue;}
        const FTransform Destination=R.DeckTransforms[BoxIndex]*New->GetActorTransform();
        R.HoldSeconds+=Dt;
        if(R.Phase==1)
        {
            R.Status=TEXT("R/S 스프레더 정렬 · 잠금 확인");
            RS->PositionRecoverySpreader(C->GetActorLocation()+FVector(0,0,C->ExternalDimensionsCm.Z*.5f),C->GetActorRotation(),C->ExternalDimensionsCm.Y);
            if(R.HoldSeconds<3) continue;
            if(C->LocationOwner!=ECargoOwner::AGV || C->GetAttachParentActor()!=Old || C->GetBody()->IsSimulatingPhysics())
            {R.Status=TEXT("화물 소유권 확인 실패");continue;}
            C->AttachToActor(RS,FAttachmentTransformRules::KeepWorldTransform);
            C->LocationOwner=ECargoOwner::ReachStacker;
            Old->SetPayload(FMath::Max(0.f,Old->PayloadKg-C->MassKg));
            R.Phase=2; R.HoldSeconds=0;
            RecordRecoveryEvent(TEXT("loaded_agv_lift"),Old,Old,RS,C);
        }
        if(R.Phase>=2 && R.Phase<=4)
        {
            R.Status=R.Phase==2?TEXT("R/S 인양"):R.Phase==3?TEXT("R/S 대체 AGV로 이송"):TEXT("대체 AGV 안착");
            FVector Goal=Destination.GetLocation();
            const float Height=FMath::Max(Old->CargoPosition().Z,New->CargoPosition().Z)+250.f;
            if(R.Phase==2) {Goal=C->GetActorLocation();Goal.Z=Height;}
            if(R.Phase==3) Goal.Z=Height;
            C->SetActorLocation(FMath::VInterpConstantTo(C->GetActorLocation(),Goal,Dt,R.Phase==3?60.f:40.f));
            C->SetActorRotation(FMath::QInterpConstantTo(C->GetActorQuat(),Destination.GetRotation(),Dt,.2f));
            RS->PositionRecoverySpreader(C->GetActorLocation()+FVector(0,0,C->ExternalDimensionsCm.Z*.5f),C->GetActorRotation(),C->ExternalDimensionsCm.Y);
            if(!C->GetActorLocation().Equals(Goal,.1f)) continue;
            ++R.Phase; R.HoldSeconds=0;
        }
        if(R.Phase==5)
        {
            R.Status=TEXT("안착 확인 · 잠금 해제");
            if(R.HoldSeconds<2 || !C->GetActorTransform().Equals(Destination,.1f)) continue;
            C->AttachToActor(New,FAttachmentTransformRules::KeepWorldTransform); C->LocationOwner=ECargoOwner::AGV;
            New->SetPayload(New->PayloadKg+C->MassKg);
            RecordRecoveryEvent(TEXT("loaded_agv_seated"),Old,RS,New,C);
            if(++R.Box<R.Cargo.Num()) {R.Phase=1;R.HoldSeconds=0;continue;}
            R.Phase=6;
        }
        if(R.Phase==6)
        {
            // Preserve the exact remaining job and destination. A local access
            // route rejoins the original route; bookings change owners once.
            const auto Saved=Jobs[R.Failed];
            TArray<FVector> Join;
            int32 Next=Saved.Waypoint;
            if(!Saved.Route.IsValidIndex(Next)) Next=Saved.Route.Num()-1;
            if(Next<0 || !PlanLoadedRecoveryRoad(New->GetActorLocation(),Saved.Route[Next],Old->GetActorRightVector(),R.Replacement,Join))
            {R.Status=TEXT("화물 인계 완료 · 원래 운송 경로 재진입 대기");continue;}
            Arrival=Saved;
            const int32 Added=Join.Num();
            for(int32 N=Next+1;N<Saved.Route.Num();++N) Join.Add(Saved.Route[N]);
            Arrival.Route=MoveTemp(Join); Arrival.Waypoint=0; Arrival.RouteStart=New->GetActorLocation(); Arrival.bRouteStartValid=true;
            Arrival.YardEntryWaypoint=Saved.YardEntryWaypoint>=Next?Added-1+Saved.YardEntryWaypoint-Next:Saved.YardEntryWaypoint;
            if(Arrival.Stage==9) {Arrival.Stage=3;Arrival.YardEntryWaypoint=Added-1;}
            Arrival.bEquipmentWaiting=false; Arrival.StationaryTime=0;
            New->ActiveDeckOffsetCm=Old->ActiveDeckOffsetCm;
            for(auto& LeaseOwner:STSOwners) if(LeaseOwner==R.Failed) LeaseOwner=R.Replacement;
            for(auto& LeaseOwner:NextVehicles) if(LeaseOwner==R.Failed) LeaseOwner=R.Replacement;
            for(auto& LeaseOwner:YardApproachOwners) if(LeaseOwner==R.Failed) LeaseOwner=R.Replacement;
            for(auto& Lease:DeferredYardLeases) if(Lease.Vehicle==R.Failed) Lease.Vehicle=R.Replacement;
            for(const auto& Crane:Equipment) if(Crane->GetHandoverVehicle()==Old) Crane->SetHandoverVehicle(New);
            Jobs[R.Failed]=FSiteTransfer(); Old->ActiveDeckOffsetCm=0;
            for(int32 ID:{Old->VehicleID,New->VehicleID})
            {
                RoadReservations.Remove(ID); RoadReservationEndWaypoint.Remove(ID); ReservationIndex.Remove(ID);
                RoadTargets.Remove(ID); RoadBlockers.Remove(ID); RoadWaitSince.Remove(ID); FinishedRoadSegments.Remove(ID); ClearPendingRoadRequest(ID);
            }
            RecordRecoveryEvent(TEXT("loaded_agv_resumed"),Old,Old,New,Arrival.Actor.Get());
            New->SetFMSHold(false);
            LoadedAGVRecoveries.RemoveAt(I);
        }
    }
}

void APortSiteLogistics::ResetLoadedAGVRecoveries()
{
    for(auto& R:LoadedAGVRecoveries) if(R.Stacker.IsValid()) R.Stacker->SetActorTransform(R.StackerHome);
    LoadedAGVRecoveries.Reset();
}
