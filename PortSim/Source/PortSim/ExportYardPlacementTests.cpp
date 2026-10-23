#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortContainerActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "VesselInput.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FExportYardPlacementTest,"PortSim.VesselInput.ExportYardPlacement",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FExportYardPlacementTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("World"),World)) return false;
    auto* Site=World->SpawnActor<APortSiteLogistics>();
    const FVector Destination(-4900,0,569.5);
    Site->AddShipCargo(Destination,0,0,FName(TEXT("EXPORT_NEW")),true);
    Site->AddShipCargo(Destination,0,0,FName(TEXT("IMPORT_NEW")));
    TestEqual(TEXT("LOAD excluded from discharge count"),Site->InitialShipCount(),1);
    auto* Export=Site->ExportCargo[0].Actor.Get();
    TestEqual(TEXT("New CSV ID preserved"),Export->ContainerID,FName(TEXT("EXPORT_NEW")));
    TestTrue(TEXT("Internal IDs remain unique"),Site->ExportCargo[0].ID!=Site->Manifest[0].ID);
    // Supply one ground pad in each zone; selection must match size and handling.
    for(int32 Block=0;Block<23;++Block)
    {
        FSiteYardSlot Slot;
        Slot.Block=Block; Slot.Reserved=true; Slot.Occupied=false;
        Slot.LengthFt=Export->LengthFt; Slot.Position=FVector(20000,Block*3000,149.5);
        Site->Yard.Add(Slot);
    }
    Site->ReceivingCapacity=Site->Yard.Num();
    TestTrue(TEXT("Export receives a yard pad"),Site->PlaceExportCargo());
    const int32 Pad=Site->ExportCargo[0].PlannedSlot;
    if(TestTrue(TEXT("Valid yard pad"),Site->Yard.IsValidIndex(Pad)))
    {
        TestTrue(TEXT("Actor is at yard pad"),Export->GetActorLocation().Equals(Site->Yard[Pad].Position));
        TestTrue(TEXT("Pad occupied and excluded from discharge reservations"),
            Site->Yard[Pad].Occupied && !Site->Yard[Pad].Reserved);
        TestTrue(TEXT("Destination retained"),Site->ExportCargo[0].Transform.GetLocation().Equals(Destination));
        TestTrue(TEXT("Yard ownership"),Export->LocationOwner==ECargoOwner::Yard);
        TestFalse(TEXT("Visible at reset"),Export->IsHidden());
        TestEqual(TEXT("Initial yard inventory"),Site->InitialYard,1);
        TestEqual(TEXT("Receiving inventory"),Site->ReceivingCapacity,Site->Yard.Num()-1);
    }
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVesselOperationsValidationTest,"PortSim.VesselInput.OperationsAgainstStowage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVesselOperationsValidationTest::RunTest(const FString& Parameters)
{
    FPortVesselInput Stowage,Operations;
    FPortVesselInputRow Lower; Lower.ContainerId=TEXT("LOWER"); Lower.GridSlot=FIntVector(0,0,0);
    FPortVesselInputRow Upper; Upper.ContainerId=TEXT("UPPER"); Upper.GridSlot=FIntVector(0,0,1);
    Stowage.Rows={Lower,Upper};
    FString Error;
    TestTrue(TEXT("No operations retains stowage"),ValidateVesselOperations(Stowage,Operations,Error));
    Lower.Operation=TEXT("DISCHARGE"); Operations.Rows={Lower};
    TestFalse(TEXT("Cannot discharge below retained cargo"),ValidateVesselOperations(Stowage,Operations,Error));
    Upper.Operation=TEXT("DISCHARGE"); Operations.Rows.Add(Upper);
    FPortVesselInputRow Load; Load.ContainerId=TEXT("NEW"); Load.Operation=TEXT("LOAD"); Load.GridSlot=FIntVector(0,0,0);
    Operations.Rows.Add(Load);
    TestTrue(TEXT("Can reuse a discharged slot"),ValidateVesselOperations(Stowage,Operations,Error));
    Operations.Rows.Last().GridSlot.Z=1;
    TestFalse(TEXT("Cannot load an unsupported upper tier"),ValidateVesselOperations(Stowage,Operations,Error));
    Operations.Rows={Upper}; Operations.Rows[0].ContainerId=TEXT("MISSING");
    TestFalse(TEXT("Missing discharge inventory is rejected"),ValidateVesselOperations(Stowage,Operations,Error));
    return true;
}
#endif
