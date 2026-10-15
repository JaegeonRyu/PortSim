#pragma once
#include "CoreMinimal.h"
#include "CargoInformation.h"

struct FPortShipStowageAssumptions
{
    FString Basis=TEXT("simulation_assumptions_not_vessel_approved");
    FString SnapshotJson;
    double LightshipKg=30000000, LightshipKGM=9, KMM=12, FreeSurfaceMomentKgM=3000000;
    double MinimumGMM=.5, MaximumGMM=4.5, MaximumHeelDegrees=5;
    double MaximumStackKg=150000, MaximumLashingN=200000, LateralAccelerationMps2=1.47;
    double DeckHeightM=12;
    int32 Bays=10, Rows=8, Tiers=8;
    bool Load(const FString& Path,FString& Error);
};
struct FPortShipPlanningCargo
{
    int32 ID=0, LengthFt=40;
    double GrossKg=0;
    FPortCargoInformation Information;
    FVector CoGOffsetM=FVector::ZeroVector; // ship axes: X longitudinal, Y transverse, Z vertical.
};
struct FPortShipPlacement
{
    int32 CargoID=0, Bay=0, Row=0, Tier=0;
    double CorrectedGMM=0, HeelDegrees=0, StackKg=0, LashingN=0;
};
struct FPortShipStowageResult
{
    bool Feasible=false;
    FString Reason;
    TArray<FPortShipPlacement> Placements;
    double CorrectedGMM=0, HeelDegrees=0;
};

// A loading-plan preview. It does not move actors or approve a real ship's loading.
FPortShipStowageResult PlanShipStowage(const TArray<FPortShipPlanningCargo>& Cargo,
    const FPortShipStowageAssumptions& A);
