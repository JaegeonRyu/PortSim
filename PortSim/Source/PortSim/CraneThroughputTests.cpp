#if WITH_DEV_AUTOMATION_TESTS
#include "STSOperatingProfile.h"
#include "CraneTravelBraking.h"
#include "YardFlowEstimate.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCraneTravelResponseTest,"PortSim.STS.TravelResponse",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCraneTravelResponseTest::RunTest(const FString& Parameters)
{
    FSTSOperatingProfile P;
    if(!TestTrue(TEXT("Load operating profile"),P.Load(FPaths::ProjectDir()/TEXT("../Document/STS/STS_ReferenceData.json"),
        FPaths::ProjectConfigDir()/TEXT("STS_Simulation.json")))) return false;
    for(double Length:{12.,25.,40.}) for(double Wind:{0.,5.})
    {
        auto C=P.Dynamics; C.Wind=FVector(Wind,0,0);
        FSTSSuspension Plant;
        double X=0,V=0,SampleX=0,SampleV=0,Angle=0,Rate=0,LastOutside=0,Peak=0;
        constexpr double H=1./120.,Mass=25300.;
        const double Offset=C.WindEquilibriumOffset(Length,Mass).X;
        const double Equilibrium=FMath::Asin(FMath::Clamp(Offset/Length,-1.,1.));
        for(int32 Step=0;Step<120*100;++Step)
        {
            // Sampled 20 Hz feedback, while the plant runs at 120 Hz.
            if(Step%6==0) { SampleX=X; SampleV=V; Angle=Plant.Angle.X; Rate=Plant.Rate.X; }
            const double Error=40-Offset-SampleX-SampleV*(Step%6)*H;
            double A=C.TravelAcceleration(Error,SampleV,Length,0,Rate,Angle,Equilibrium);
            A=CraneTravelBraking::Acceleration(Error*100,V*100,A*100,80,100,H)*.01;
            const FVector2D Limited=CraneTravelBraking::SwayAcceleration(FVector2D(Angle,0),FVector2D(Rate,0),
                FVector2D(C.AerodynamicForce(FVector(V,0,0)).X/Mass,0),FVector2D(A,0),FVector2D(.8,.8),
                Length,0,C.Damping,FMath::DegreesToRadians(12.),H);
            const double Next=FMath::Clamp(V+Limited.X*H,-4.,4.);
            A=(Next-V)/H; V=Next; X+=V*H;
            Plant.Step(C,H,Length,0,FVector(A,0,0),FVector(V,0,0),Mass,FVector(.2,0,0),P.HoistPowerW);
            Peak=FMath::Max(Peak,Plant.SwayDegrees());
            if(FMath::Abs(40-X-Plant.Offset.X)>.05 || FMath::Abs(V+Plant.OffsetVelocity.X)>.05 || Plant.SwayDegrees()>1)
                LastOutside=Step*H;
        }
        UE_LOG(LogTemp,Display,TEXT("TRAVEL_RESPONSE: rope=%.0f wind=%.0f settled=%.2f peak_sway=%.2f"),Length,Wind,LastOutside,Peak);
        TestTrue(TEXT("40 m travel settles within 60 seconds with sampled sensors"),LastOutside<60);
        TestTrue(TEXT("Rope forces remain valid and sway inside sensor envelope"),Plant.Fault.IsEmpty() && Peak<20);
    }
    auto Legacy=P.Dynamics; Legacy.TravelResponseRatio=0;
    TestEqual(TEXT("Explicit zero retains configured legacy feedback"),Legacy.TravelAcceleration(2,.1,25,0,.01,.02,0),
        Legacy.HorizontalAcceleration(2,.1,25,.01,.02,0));
    auto Disabled=P.Dynamics; Disabled.AntiSway=false;
    TestEqual(TEXT("Disabling anti-sway does not silently re-enable feedback"),Disabled.TravelAcceleration(2,.1,25,0,.01,.02,0),
        Disabled.HorizontalAcceleration(2,.1,25,.01,.02,0));
    const auto BusyStack=YardFlowEstimate::Evaluate(0,800,700,100,40,false,false);
    const auto FreeFloor=YardFlowEstimate::Evaluate(0,0,0,140,40,true,false);
    TestTrue(TEXT("Free compatible floor beats a blocked stack despite longer travel"),FreeFloor.Cost<BusyStack.Cost);
    const auto FreeStack=YardFlowEstimate::Evaluate(800,0,700,100,40,false,false);
    const auto SameFloor=YardFlowEstimate::Evaluate(800,0,0,100,40,true,false);
    TestTrue(TEXT("Free floor avoids a needless forecast support dependency"),SameFloor.Cost<FreeStack.Cost);
    const auto DistantFloor=YardFlowEstimate::Evaluate(800,0,0,280,40,true,false);
    TestTrue(TEXT("Compatible stack remains worthwhile when it saves enough travel"),FreeStack.Cost<DistantFloor.Cost);
    TestTrue(TEXT("Approach forecast includes the outbound empty vehicle"),FreeFloor.Released>FreeFloor.Placed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSTSCoordinatedTravelTest,"PortSim.STS.CoordinatedTransfer",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSTSCoordinatedTravelTest::RunTest(const FString& Parameters)
{
    FSTSOperatingProfile Profile;
    if(!TestTrue(TEXT("Load STS reference for coordinated travel"),Profile.Load(
        FPaths::ProjectDir()/TEXT("../Document/STS/STS_ReferenceData.json"),
        FPaths::ProjectConfigDir()/TEXT("STS_Simulation.json")))) return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto Run=[&](int32 LengthFt,double Wind,bool Coordinated,bool Obstacle)
    {
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        const FVector Home(100000,0,0),Source=Home+FVector(-3000,0,500),Destination=Home+FVector(3000,0,349.5);
        auto* Crane=World->SpawnActor<APortWorkingCrane>(Home,FRotator::ZeroRotator);
        Crane->SetSTSProfile(Profile);
        Crane->bCoordinatedSTSTravel=Coordinated;
        Crane->Configure(1,true,Source,Destination);
        auto* Cargo=Crane->CargoActor.Get();
        FPortContainerSpecification Specification;
        Specification.Size=LengthFt==20?EPortContainerSize::Ft20:LengthFt==45?EPortContainerSize::Ft45:EPortContainerSize::Ft40;
        Specification.LengthFt=LengthFt;
        Specification.LengthCm=LengthFt==20?605.8f:LengthFt==45?1371.6f:1219.2f;
        Cargo->ConfigureSpecification(Specification);
        Crane->TelescopeTargetLengthCm=Cargo->ExternalDimensionsCm.Y;
        Cargo->SetPhysicalParameters(LengthFt==20?14300:LengthFt==45?29300:24300,FVector::ZeroVector);
        Cargo->GetBody()->SetSimulatePhysics(false);
        for(const auto& Pad:Crane->Pads)
        {
            Pad->SetWorldScale3D(FVector(Cargo->ExternalDimensionsCm.X+66,Cargo->ExternalDimensionsCm.Y+80,20)/100.);
            // STS support observations exclude its own geometry. Represent the
            // receiving platform as an independent physical support surface.
            auto* Support=World->SpawnActor<AStaticMeshActor>(Pad->GetComponentLocation(),Pad->GetComponentRotation());
            Support->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Support->GetStaticMeshComponent()->SetStaticMesh(Pad->GetStaticMesh());
            Support->SetActorTransform(Pad->GetComponentTransform());
            Support->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAllDynamic"));
            Pad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
        Crane->SetWindVelocity(FVector(Wind,0,0));
        APortContainerActor* Blocker=nullptr;
        if(Obstacle)
        {
            Blocker=World->SpawnActor<APortContainerActor>(Home+FVector(0,0,1100),FRotator::ZeroRotator);
            Blocker->InitializeContainer(9001);
            Blocker->GetBody()->SetSimulatePhysics(false);
        }
        bool RaisedWhileTravelling=false,ClearOfObstacle=true,LocksValid=true;
        for(int32 Step=0;Step<60*300 && !Crane->CompletedJobs && Crane->Fault.IsEmpty();++Step)
        {
            Crane->Advance(1.f/60,false);
            if(Crane->bCarrying)
            {
                LocksValid &= Crane->Observation.AllLocked();
                RaisedWhileTravelling |= FMath::Abs(Crane->AxisVelocity.X)>10 && Crane->AxisVelocity.Z>10;
                if(Blocker)
                {
                    const FBox Moving=Cargo->GetBody()->Bounds.GetBox(),Fixed=Blocker->GetBody()->Bounds.GetBox();
                    if(Moving.Min.X<=Fixed.Max.X && Moving.Max.X>=Fixed.Min.X &&
                        Moving.Min.Y<=Fixed.Max.Y && Moving.Max.Y>=Fixed.Min.Y)
                        ClearOfObstacle &= Moving.Min.Z>Fixed.Max.Z;
                }
            }
        }
        const FString Case=FString::Printf(TEXT("%d ft wind=%.0f coordinated=%d obstacle=%d"),LengthFt,Wind,Coordinated,Obstacle);
        if(!Crane->CompletedJobs) UE_LOG(LogTemp,Display,TEXT("STS_TRANSFER_INCOMPLETE: %s stage=%d head=%s cargo=%s supported=%d aligned=%d fresh=%d phase=%d reason=%s fault=%s"),
            *Case,Crane->Stage,*Crane->Head.ToString(),*Cargo->GetActorLocation().ToString(),Crane->Observation.bCargoSupported,
            Crane->Observation.bAGVAligned,Crane->Observation.IsFresh(Crane->SimulationTime,Profile.SensorMaxAge),int32(Crane->Pickup.Phase),*Crane->Pickup.Reason,*Crane->Fault);
        TestEqual(Case+TEXT(" completes one transfer"),Crane->CompletedJobs,1);
        TestTrue(Case+TEXT(" keeps four-lock feedback"),LocksValid);
        TestTrue(Case+TEXT(" clears the obstruction with the full cargo body"),ClearOfObstacle);
        if(Coordinated) TestTrue(Case+TEXT(" actually hoists and travels concurrently"),RaisedWhileTravelling);
        if(!Crane->Fault.IsEmpty()) AddError(Case+TEXT(": ")+Crane->Fault);
        const double Duration=Crane->LastJobSeconds;
        UE_LOG(LogTemp,Display,TEXT("STS_COORDINATED_TRANSFER: %s seconds=%.2f"),*Case,Duration);
        World->DestroyWorld(false);
        return Duration;
    };
    for(int32 Length:{20,40,45}) for(double Wind:{0.,5.})
    {
        const double Sequential=Run(Length,Wind,false,false);
        const double Coordinated=Run(Length,Wind,true,false);
        TestTrue(TEXT("Coordinated travel reduces the same reference-limited transfer by at least five seconds"),
            Sequential>0 && Coordinated>0 && Coordinated<Sequential-5.);
    }
    Run(40,0,true,true);
    return true;
}
#endif
