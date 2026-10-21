#pragma once

#include "CoreMinimal.h"

/** Shared single-lift 20/40/45 ft spreader geometry.
 *
 * The 30 second full-stroke time follows the public Bromma SSX45/YSX45
 * reference.  The simulation moves the two end beams symmetrically; the
 * central frame does not change size.  Dimensions are centimetres/seconds.
 */
namespace PortSpreaderTelescope
{
    constexpr float Length20Cm=605.8f;
    constexpr float Length40Cm=1219.2f;
    constexpr float Length45Cm=1371.6f;
    constexpr float FullStrokeSeconds=30.f;
    constexpr float FullLengthSpeedCmPerSecond=(Length45Cm-Length20Cm)/FullStrokeSeconds;
    constexpr float PositionToleranceCm=1.f;

    // Visible simulation geometry. Keeping the aerodynamic silhouette beside
    // the mesh dimensions prevents the wind model from drifting back to a
    // single 40 ft-equivalent area when the telescope length changes.
    constexpr float WidthCm=243.8f;
    constexpr float CentreLengthCm=500.f;
    constexpr float CentreHeightCm=50.f;
    constexpr float ArmWidthCm=125.f;
    constexpr float ArmHeightCm=34.f;
    constexpr float EndBeamLengthCm=40.f;
    constexpr float EndBeamHeightCm=60.f;
    constexpr float TwistlockWidthCm=16.f;
    constexpr float TwistlockLengthCm=16.f;
    constexpr float TwistlockHeightCm=20.f;
    constexpr float TwistlockCenterZCm=-35.f;

    inline float Advance(float CurrentLengthCm,float TargetLengthCm,float DeltaSeconds)
    {
        return FMath::FInterpConstantTo(CurrentLengthCm,TargetLengthCm,DeltaSeconds,FullLengthSpeedCmPerSecond);
    }

    inline bool IsReady(float CurrentLengthCm,float TargetLengthCm)
    {
        return FMath::Abs(CurrentLengthCm-TargetLengthCm)<=PositionToleranceCm;
    }

    // ISO corner fitting centre is approximately 103 mm inboard from each end
    // and 89.5 mm inboard from each side of the external container envelope.
    inline float TwistlockHalfLengthCm(float ExternalLengthCm)
    {
        return FMath::Max(0.f,ExternalLengthCm*.5f-10.3f);
    }

    inline float TwistlockHalfWidthCm(float ExternalWidthCm=243.8f)
    {
        return FMath::Max(0.f,ExternalWidthCm*.5f-8.95f);
    }

    /**
     * Local-axis projected silhouette areas in m2. X is the YZ face seen by
     * X-directed wind, Y is the XZ face, and Z is the XY face. The calculation
     * forms the union of overlapping spreader members rather than adding every
     * box face (which would count the end beams and arms more than once).
     */
    inline FVector EmptyProjectedAreasM2(float ExternalLengthCm)
    {
        const float EndY=TwistlockHalfLengthCm(ExternalLengthCm);
        const float CentreHalfLength=CentreLengthCm*.5f;
        const float ExposedArmLength=FMath::Max(0.f,EndY-EndBeamLengthCm*.5f-CentreHalfLength);

        const float TwistlockBelowBeamCm=FMath::Max(0.f,
            -TwistlockCenterZCm+TwistlockHeightCm*.5f-EndBeamHeightCm*.5f);
        const float XAreaCm2=CentreLengthCm*CentreHeightCm+
            2.f*ExposedArmLength*ArmHeightCm+
            2.f*EndBeamLengthCm*EndBeamHeightCm+
            2.f*TwistlockLengthCm*TwistlockBelowBeamCm;
        const float YAreaCm2=WidthCm*EndBeamHeightCm+
            2.f*TwistlockWidthCm*TwistlockBelowBeamCm;
        const float ZAreaCm2=WidthCm*CentreLengthCm+
            2.f*ArmWidthCm*ExposedArmLength+2.f*WidthCm*EndBeamLengthCm;
        return FVector(XAreaCm2,YAreaCm2,ZAreaCm2)*1.e-4f;
    }

    inline FVector BoxProjectedAreasM2(FVector DimensionsCm)
    {
        const FVector M=DimensionsCm.GetAbs()*.01f;
        return FVector(M.Y*M.Z,M.X*M.Z,M.X*M.Y);
    }

    /** Loaded state exposes both the spreader frame and the selected ISO box. */
    inline FVector LoadedProjectedAreasM2(float SpreaderLengthCm,FVector ContainerDimensionsCm)
    {
        return EmptyProjectedAreasM2(SpreaderLengthCm)+BoxProjectedAreasM2(ContainerDimensionsCm);
    }
}
