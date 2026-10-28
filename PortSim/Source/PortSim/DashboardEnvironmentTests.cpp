#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortEnvironmentComponent.h"
#include "PortAGVActor.h"
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDashboardEnvironmentTest,"PortSim.Dashboard.EnvironmentObservation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDashboardEnvironmentTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto* Owner=W->SpawnActor<AActor>();auto* E=NewObject<UPortEnvironmentComponent>(Owner);
    Owner->AddInstanceComponent(E);E->RegisterComponent();
    auto* Site=W->SpawnActor<APortSiteLogistics>();Site->SetOwner(Owner);
    E->SetRainEnabled(true);E->SetRainfallMmPerHour(30);E->SetSnowEnabled(true);E->SetSnowfallMmPerHour(10);
    E->SetWindSpeedMetersPerSecond(5);E->SetWaterFilmMm(.5);
    auto Snapshot=Site->CaptureEnvironment();
    TestEqual(TEXT("Read-only web snapshot observes the Unreal rainfall"),Snapshot->GetNumberField(TEXT("rain_mm_h")),30.);
    TestEqual(TEXT("Read-only web snapshot observes actual water film"),Snapshot->GetNumberField(TEXT("water_film_mm")),.5);
    TestTrue(TEXT("Current coefficient comes from the same environment state"),FMath::IsNearlyEqual(
        Snapshot->GetNumberField(TEXT("tire_mu_static")),double(E->GetFrictionSettings().Tire.Static(E->GetSurfaceConditions())),.00001));
    W->DestroyWorld(false);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdvanceYardBookingTest,"PortSim.Logistics.BoundedNextYardService",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAdvanceYardBookingTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto* Site=W->SpawnActor<APortSiteLogistics>();Site->LaneCount=9;Site->YardCraneCount=1;
    auto* R=W->SpawnActor<APortWorkingCrane>();R->Configure(1,false,FVector(-1000,0,349.5),FVector(1000,0,149.5),false);
    Site->Equipment.Add(R);Site->BlocksBusy.Init(false,1);Site->YardApproachOwners.Init(0,1);
    Site->RMGBusy.Init(true,1);Site->SlotAssigned.Init(false,3);Site->Jobs.SetNum(3);
    for(int32 I=0;I<3;++I)
    {
        auto* V=W->SpawnActor<APortAGVActor>(FVector(I*2000,5000,0),FRotator::ZeroRotator);V->InitializeVehicle(100+I);Site->Vehicles.Add(V);
        auto* Box=W->SpawnActor<APortContainerActor>();Box->InitializeContainer(9900+I);
        FSiteShipCargo C;C.Actor=Box;C.PlannedSlot=I;Site->Manifest.Add(C);
        FSiteYardSlot S;S.Reserved=true;S.Occupied=false;S.Crane=0;S.Block=0;S.Handover=FVector(16000,0,0);Site->Yard.Add(S);
        Site->Jobs[I].Cargo=I;Site->Jobs[I].Actor=Box;Site->Jobs[I].Stage=6;
    }
    auto& Prior=Site->Jobs[0];Prior.RMG=0;Prior.Stage=3;Prior.Slot=0;Prior.bRMGReserved=true;
    TestFalse(TEXT("Cannot pipeline behind an AGV still travelling to the yard"),Site->CanBookNextYardService(1));
    TestTrue(TEXT("Create active prior crane job"),R->AssignCargo(Site->Manifest[0].Actor.Get(),FVector(-1000,0,349.5),FVector(1000,0,149.5),false,true));
    Prior.Stage=4;R->Stage=2;R->bCarrying=false;
    TestFalse(TEXT("Cannot speculate before the prior pickup is verified"),Site->CanBookNextYardService(1));
    R->Stage=3;R->bCarrying=true;
    Site->Yard[1].Below=0;
    TestFalse(TEXT("Missing support remains a hard prohibition"),Site->CanBookNextYardService(1));
    Site->Yard[1].Below=INDEX_NONE;
    const FTransform Before=Site->Vehicles[1]->GetActorTransform();
    TestTrue(TEXT("Next STS job can reserve one future service"),Site->ReserveYardApproach(1,true));
    TestTrue(TEXT("Actual approach stays with outgoing job; vehicle remains at STS"),
        Site->YardApproachOwners[0]==0 && Site->Jobs[1].bYardAdvanceBooking && Site->Vehicles[1]->GetActorTransform().Equals(Before));
    TestFalse(TEXT("A third job cannot join the same future queue"),Site->CanBookNextYardService(2));
    TestFalse(TEXT("Future booking does not authorize driving to the yard"),Site->ReserveYardApproach(1));
    Site->YardApproachOwners[0]=INDEX_NONE;Prior.bYardReleased=true;Site->RMGBusy[0]=false;
    TestFalse(TEXT("New arrivals cannot steal the released approach from the booked job"),Site->YardApproachAvailable(2));
    TestTrue(TEXT("Only actual release promotes the booked job to road admission"),Site->ReserveYardApproach(1));
    TestTrue(TEXT("Promotion transfers ownership and clears future flag"),Site->YardApproachOwners[0]==1 && !Site->Jobs[1].bYardAdvanceBooking);
    W->DestroyWorld(false);return true;
}
#endif
