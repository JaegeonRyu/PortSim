#include "PortEquipmentActor.h"
#include "TerminalLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

APortEquipmentActor::APortEquipmentActor()
{
    PrimaryActorTick.bCanEverTick=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    RootComponent->SetMobility(EComponentMobility::Movable);
}

UStaticMeshComponent* APortEquipmentActor::Box(const TCHAR* Name,USceneComponent* Parent,FVector Position,FVector Size,bool Collision)
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Mesh->SetupAttachment(Parent);
    Mesh->SetStaticMesh(Cube.Object);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetRelativeLocation(Position);
    Mesh->SetRelativeScale3D(Size/100.f);
    Mesh->SetCollisionProfileName(Collision?TEXT("BlockAllDynamic"):TEXT("NoCollision"));
    Mesh->SetGenerateOverlapEvents(false); // Explicit sensor queries retain blocking collision.
    return Mesh;
}

UTextRenderComponent* APortEquipmentActor::Label(const TCHAR* Name,FVector Position)
{
    auto* Text=CreateDefaultSubobject<UTextRenderComponent>(Name);
    Text->SetupAttachment(RootComponent);
    Text->SetWorldSize(100.f);
    Text->SetHorizontalAlignment(EHTA_Center);
    Text->SetRelativeLocation(Position);
    Text->SetRelativeRotation(FRotator(90,0,0));
    return Text;
}

