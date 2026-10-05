#include "PortContainerActor.h"
#include "PortEnvironmentComponent.h"
#include "TerminalLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

APortContainerActor::APortContainerActor()
{
    PrimaryActorTick.bCanEverTick=false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContainerBody"));
    SetRootComponent(Body);
    Body->SetStaticMesh(Cube.Object);
    Body->SetWorldScale3D(FVector(2.44f,12.2f,2.59f));
    Body->SetMobility(EComponentMobility::Movable);
    Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Body->SetSimulatePhysics(true);
    Body->SetLinearDamping(.4f);
    Body->SetAngularDamping(2.f);
    Body->BodyInstance.bUseCCD=true;
    Body->BodyInstance.PositionSolverIterationCount=16;
    Body->BodyInstance.VelocitySolverIterationCount=8;
    Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContainerVisual"));
    Visual->SetupAttachment(Body);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetAbsolute(false,false,true);
    Tags.Add(TEXT("PortSim.Container"));
}

void APortContainerActor::BeginPlay()
{
    Super::BeginPlay();
    SetPhysicalParameters(MassKg,CoGOffsetCm);
    ApplyContainerAppearance();
}

void APortContainerActor::SetPhysicalParameters(float Mass,FVector CoGOffset)
{
    MassKg=Mass; CoGOffsetCm=CoGOffset;
    Body->SetMassOverrideInKg(NAME_None,MassKg);
    Body->SetCenterOfMass(CoGOffsetCm);
}

void APortContainerActor::ApplyWind(FVector WindVelocityMetersPerSecond,float DragCoefficient)
{
    if(!Body->IsSimulatingPhysics()) return;
    const FVector DimensionsMeters(2.44,12.2,2.59);
    const FVector ForceNewtons=FPortWindAerodynamics::DragForceNewtons(WindVelocityMetersPerSecond,
        Body->GetPhysicsLinearVelocity(),Body->GetComponentQuat(),DimensionsMeters,DragCoefficient);
    // Chaos uses centimetres, so one SI newton is 100 kg*cm/s^2.
    Body->AddForce(ForceNewtons*100.f);
}

void APortContainerActor::SetSecuredVesselMotion(FVector Position,FQuat Rotation,FVector VelocityCentimetersPerSecond)
{
    if(Body->IsSimulatingPhysics()) Body->SetSimulatePhysics(false);
    SetActorLocationAndRotation(Position,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
    KinematicVelocityCentimetersPerSecond=VelocityCentimetersPerSecond;
}

FVector APortContainerActor::GetMotionVelocity() const
{
    return Body->IsSimulatingPhysics()?Body->GetPhysicsLinearVelocity():KinematicVelocityCentimetersPerSecond;
}

void APortContainerActor::InitializeContainer(int32 Number)
{
    ContainerID=FName(*FString::Printf(TEXT("C%02d"),Number));
#if WITH_EDITOR
    SetActorLabel(FString::Printf(TEXT("Container_%s"),*ContainerID.ToString()));
    SetFolderPath(TEXT("PortSim/Containers"));
#endif
}

void APortContainerActor::ResetCargo(FVector Position)
{
    Body->SetSimulatePhysics(false);
    SetActorLocationAndRotation(Position,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
    Body->SetSimulatePhysics(true);
    Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
    Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    Body->WakeAllRigidBodies();
    LocationOwner=ECargoOwner::Ship;
}

void APortContainerActor::ApplyContainerAppearance()
{
    auto* Model=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/PortSim/Assets/Models/Container_Quaternius/Container_Quaternius/StaticMeshes/Container_Quaternius.Container_Quaternius"));
    if (!Model) return;
    Visual->SetStaticMesh(Model);
    const FBoxSphereBounds Bounds=Model->GetBounds();
    const FVector Size=Bounds.BoxExtent*2.f;
    const FVector Scale(1220.f/Size.X,244.f/Size.Y,259.f/Size.Z);
    const FRotator Rotation(0,90,0);
    Visual->SetRelativeRotation(Rotation);
    Visual->SetRelativeScale3D(Scale);
    Visual->SetRelativeLocation(-Rotation.RotateVector(Bounds.Origin*Scale)/Body->GetComponentScale());
    Body->SetVisibility(false,false);
}

