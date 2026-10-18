#pragma once

#include "CoreMinimal.h"
#include "PortEquipmentActor.h"
#include "PortAGVActor.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

USTRUCT(BlueprintType)
struct FAGVSensorState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") FVector EstimatedPosition=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") FVector LastAbsolutePosition=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") float PositionErrorCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") float WheelOdometryCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") float SteeringAngleDegrees=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") float LateralSlipCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") bool bReversing=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") int32 CurrentTransponderID=INDEX_NONE;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Navigation") bool bTransponderLocked=false;

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|FMS") FVector FMSNextNode=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|FMS") int32 FMSNodeIndex=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|FMS") int32 FMSNodeCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|FMS") bool bFMSConnected=true;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|FMS") bool bFMSHold=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|FMS") bool bDockingNode=false;

    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Safety") float FrontObstacleDistanceCm=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Safety") float RearObstacleDistanceCm=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Safety") float LiDARSafetyDistanceCm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Safety") float LiDARDetectionRangeCm=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Safety") int32 LiDARBlockingVehicleID=INDEX_NONE;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Safety") bool bLiDARClear=true;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AGV|Safety") bool bControlledStop=false;
};

UCLASS(Blueprintable)
class PORTSIM_API APortAGVActor : public APortEquipmentActor
{
    GENERATED_BODY()
public:
    APortAGVActor();
    void InitializeVehicle(int32 Number);
    void ResetVehicle(FVector Position);
    bool MoveToX(float X, float Dt);
    bool MoveToPosition(FVector Target, float Dt, bool StopAtTarget=true);
    FVector PlannedMotionDirection(FVector Target) const;
    FVector CargoPosition() const;
    void SetPayload(float GrossMassKg) { PayloadKg=FMath::Max(0.f,GrossMassKg); }
    void SetFMSCommand(FVector Target,int32 NodeIndex,int32 NodeCount,bool Docking,float AcceptanceCm=-1.f,
        FVector ExpectedDockingAxis=FVector::ZeroVector);
    void SetFMSHold(bool Hold);
    void UpdateLiDARObservation(float FrontDistanceCm,float RearDistanceCm,float SafetyDistanceCm,float DetectionRangeCm,
        bool ControlledStop,int32 BlockingVehicleID=INDEX_NONE);
    float SpeedLimitCmPerSecond() const;
    void SetApproachSpeedLimit(float Limit) { FMSApproachSpeedLimit=Limit; }
    void SetWetTractionScale(float Scale) { WetTractionScale=FMath::Clamp(Scale,.5f,1.f); }
    void SetSteeringLookAhead(FVector Target,bool Enabled) { SteeringLookAhead=Target; bSteeringLookAhead=Enabled; }
    float AccelerationCmPerSecondSquared() const;
    bool PayloadWithinReferenceLimit() const;
    // Usable 60 percentage points: 8 operating hours, 30-minute fast charge.
    static constexpr double LowBatteryPercent=20.;
    static constexpr double ChargeLimitPercent=80.;
    void AdvanceBattery(double Seconds,bool Operating,bool Charging);
    bool CanAcceptWork() const { return BatteryPercent>LowBatteryPercent && !bNeedsCharge && !bCharging; }
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Battery") double BatteryPercent=80.;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Battery") bool bNeedsCharge=false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Battery") bool bCharging=false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV") int32 VehicleID=1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV") float Speed=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV") int32 CompletedJobs=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV") float PayloadKg=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV") FAGVSensorState Sensors;
private:
    void RefreshBatteryLabel();
    int32 DisplayedBattery=-1;
    bool bDisplayedCharging=false;
    void UpdateOdometry(FVector PreviousPosition);
    void CorrectAtTransponder(FVector Position);
    float FMSAcceptanceCm=800.f;
    float FMSApproachSpeedLimit=MAX_flt;
    float WetTractionScale=1.f;
    FVector SteeringLookAhead=FVector::ZeroVector;
    bool bSteeringLookAhead=false;
    FVector DockingAxis=FVector::ZeroVector;
    UPROPERTY() TObjectPtr<UTextRenderComponent> NameLabel;
};
