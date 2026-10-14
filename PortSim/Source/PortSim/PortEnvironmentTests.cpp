#if WITH_DEV_AUTOMATION_TESTS
#include "PortEnvironmentComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortWindStateTest,"PortSim.Environment.WindState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortWindStateTest::RunTest(const FString& Parameters)
{
    FPortWindState Wind; Wind.Initialize(12345);
    TestTrue(TEXT("Initial wind is enabled"),Wind.bWindEnabled);
    TestTrue(TEXT("Initial speed range"),Wind.WindSpeedMetersPerSecond>=1 && Wind.WindSpeedMetersPerSecond<=7);
    TestTrue(TEXT("Initial from bearing range"),Wind.WindDirectionDegrees>=0 && Wind.WindDirectionDegrees<360);
    TestTrue(TEXT("Target interval 20..60 simulation seconds"),Wind.SecondsUntilTarget>=20 && Wind.SecondsUntilTarget<=60);
    TestTrue(TEXT("Initial gust interval 30..90 simulation seconds"),Wind.SecondsUntilGust>=30 && Wind.SecondsUntilGust<=90);
    TestFalse(TEXT("No gust active at initialization"),Wind.bGustActive);
    auto* Component=NewObject<UPortEnvironmentComponent>();
    TestTrue(TEXT("Component defaults ON"),Component->IsWindEnabled());
    const FPortWindState Before=Component->GetWindState();
    TestTrue(TEXT("Effective velocity uses metres/second"),FMath::IsNearlyEqual(Component->GetEffectiveWindVelocity().Size(),double(Component->GetWindSpeedMetersPerSecond()),.0001));
    Component->ToggleWind();
    TestFalse(TEXT("Toggle OFF"),Component->IsWindEnabled());
    TestTrue(TEXT("OFF effective velocity is zero"),Component->GetEffectiveWindVelocity().IsZero());
    Component->AdvanceEnvironment(100);
    TestEqual(TEXT("OFF retains raw speed"),Component->GetWindSpeedMetersPerSecond(),Before.WindSpeedMetersPerSecond);
    TestEqual(TEXT("OFF retains raw direction"),Component->GetWindDirectionDegrees(),Before.WindDirectionDegrees);
    TestEqual(TEXT("OFF retains target speed"),Component->GetWindState().TargetWindSpeedMetersPerSecond,Before.TargetWindSpeedMetersPerSecond);
    TestEqual(TEXT("OFF retains target direction"),Component->GetWindState().TargetWindDirectionDegrees,Before.TargetWindDirectionDegrees);
    Component->ToggleWind();
    TestTrue(TEXT("Toggle ON restores magnitude"),Component->IsWindEnabled() && FMath::IsNearlyEqual(Component->GetEffectiveWindVelocity().Size(),double(Before.WindSpeedMetersPerSecond),.0001));
    const TCHAR* Names[]={TEXT("N"),TEXT("NNE"),TEXT("NE"),TEXT("ENE"),TEXT("E"),TEXT("ESE"),TEXT("SE"),TEXT("SSE"),TEXT("S"),TEXT("SSW"),TEXT("SW"),TEXT("WSW"),TEXT("W"),TEXT("WNW"),TEXT("NW"),TEXT("NNW")};
    for(int32 I=0;I<16;++I) TestEqual(FString::Printf(TEXT("Bearing %g"),I*22.5),FPortWindState::DirectionName(I*22.5f),FString(Names[I]));
    TestEqual(TEXT("North wrap"),FPortWindState::DirectionName(359),FString(TEXT("N")));
    TestEqual(TEXT("Sector boundary"),FPortWindState::DirectionName(11.25),FString(TEXT("NNE")));
    TestTrue(TEXT("North wind travels south"),FPortWindState::FlowDirection(0).Equals(FVector(-1,0,0),.0001));
    TestTrue(TEXT("East wind travels west"),FPortWindState::FlowDirection(90).Equals(FVector(0,-1,0),.0001));
    const FVector Dimensions(2.44,12.2,2.59);
    const FVector Broadside=FPortWindAerodynamics::DragForceNewtons(FVector(5,0,0),FVector::ZeroVector,
        FQuat::Identity,Dimensions,1.2);
    const FVector EndOn=FPortWindAerodynamics::DragForceNewtons(FVector(0,5,0),FVector::ZeroVector,
        FQuat::Identity,Dimensions,1.2);
    const double ExpectedBroadside=.5*FPortWindAerodynamics::AirDensityKgPerCubicMeter*1.2*(12.2*2.59)*25.;
    TestTrue(TEXT("Container drag uses SI force and projected broadside area"),
        FMath::Abs(Broadside.X-ExpectedBroadside)<.01 && Broadside.Y==0 && Broadside.Z==0);
    TestTrue(TEXT("Container broadside drag exceeds end-on drag"),Broadside.Size()>EndOn.Size()*4.9);
    TestTrue(TEXT("No drag at zero relative air speed"),FPortWindAerodynamics::DragForceNewtons(FVector(5,0,0),
        FVector(500,0,0),FQuat::Identity,Dimensions,1.2).IsNearlyZero());
    const float Forward=FPortWindState::InterpolateDirection(359,1,1);
    const float Reverse=FPortWindState::InterpolateDirection(1,359,1);
    TestTrue(TEXT("359 to 1 takes positive short arc"),Forward>359 && Forward<360);
    TestTrue(TEXT("1 to 359 takes negative short arc"),Reverse>0 && Reverse<1);
    TestTrue(TEXT("Large delta remains normalized"),FPortWindState::InterpolateDirection(359,1,10000)<360);
    bool Bounded=true,Changed=false,SawGust=false;
    float MaximumObservedGust=0;
    const float InitialDirection=Wind.WindDirectionDegrees;
    for(int32 I=0;I<20000;++I)
    {
        Wind.Advance(.5);
        Bounded &= Wind.WindSpeedMetersPerSecond>=1 && Wind.WindSpeedMetersPerSecond<=7 &&
            Wind.WindDirectionDegrees>=0 && Wind.WindDirectionDegrees<360 &&
            Wind.TargetWindSpeedMetersPerSecond>=1 && Wind.TargetWindSpeedMetersPerSecond<=7 &&
            Wind.TargetWindDirectionDegrees>=0 && Wind.TargetWindDirectionDegrees<360 &&
            Wind.GustSpeedMetersPerSecond>=0 && Wind.GustSpeedMetersPerSecond<=FPortWindState::MaximumGustPeak &&
            Wind.EffectiveVelocity().Size()<=FPortWindState::MaximumSpeed+FPortWindState::MaximumGustPeak+.001;
        Changed |= FMath::Abs(FMath::FindDeltaAngleDegrees(InitialDirection,Wind.WindDirectionDegrees))>1;
        SawGust |= Wind.bGustActive;
        MaximumObservedGust=FMath::Max(MaximumObservedGust,Wind.GustSpeedMetersPerSecond);
    }
    TestTrue(TEXT("10000 simulated seconds remain within weather bounds"),Bounded);
    TestTrue(TEXT("Targets produce changing weather"),Changed);
    TestTrue(TEXT("Scheduled gusts occur"),SawGust && Wind.GustCount>0);
    TestTrue(TEXT("Gust envelope reaches a material peak"),MaximumObservedGust>=FPortWindState::MinimumGustPeak);
    FPortWindState Fine,Coarse;Fine.Initialize(9876);Coarse.Initialize(9876);
    for(int32 I=0;I<16000;++I)Fine.Advance(.0625);
    for(int32 I=0;I<1000;++I)Coarse.Advance(1.);
    TestTrue(TEXT("Equal simulation time at 1x and 16x preserves speed"),FMath::Abs(Fine.WindSpeedMetersPerSecond-Coarse.WindSpeedMetersPerSecond)<.002);
    TestTrue(TEXT("Equal simulation time preserves bearing"),FMath::Abs(FMath::FindDeltaAngleDegrees(Fine.WindDirectionDegrees,Coarse.WindDirectionDegrees))<.02);
    TestEqual(TEXT("Equal simulation time preserves gust count"),Fine.GustCount,Coarse.GustCount);
    TestEqual(TEXT("Equal simulation time preserves gust activity"),Fine.bGustActive,Coarse.bGustActive);
    TestTrue(TEXT("Equal simulation time preserves gust speed"),FMath::Abs(Fine.GustSpeedMetersPerSecond-Coarse.GustSpeedMetersPerSecond)<.002);
    TestTrue(TEXT("Equal simulation time preserves effective wind vector"),Fine.EffectiveVelocity().Equals(Coarse.EffectiveVelocity(),.002));
    // Deterministic distribution check across independent initial conditions.
    int32 Bands[6]={};
    for(int32 Seed=0;Seed<12000;++Seed)
    {
        FPortWindState Sample;Sample.Initialize(Seed);
        ++Bands[FMath::Clamp(FMath::FloorToInt(Sample.WindSpeedMetersPerSecond)-1,0,5)];
    }
    for(int32 I=0;I<6;++I) if(I!=2) TestTrue(TEXT("3..4 m/s is the most populated band"),Bands[2]>Bands[I]);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortSeaStateTest,"PortSim.Environment.SeaState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortSeaStateTest::RunTest(const FString& Parameters)
{
    FPortSeaState Sea; Sea.Initialize(24680);
    TestTrue(TEXT("Sea motion defaults ON"),Sea.bSeaMotionEnabled);
    TestTrue(TEXT("Sheltered wave height is bounded"),Sea.WaveHeightMeters>=FPortSeaState::MinimumWaveHeightMeters &&
        Sea.WaveHeightMeters<=FPortSeaState::MaximumWaveHeightMeters);
    TestTrue(TEXT("Sheltered swell height is bounded"),Sea.SwellHeightMeters>=FPortSeaState::MinimumSwellHeightMeters &&
        Sea.SwellHeightMeters<=FPortSeaState::MaximumSwellHeightMeters);
    TestTrue(TEXT("Combined significant height remains in the KMA low sea band"),Sea.SignificantWaveHeightMeters()<1.f);
    TestTrue(TEXT("Wave and swell periods are distinct"),Sea.WavePeriodSeconds>=4.5f && Sea.WavePeriodSeconds<=7.f &&
        Sea.SwellPeriodSeconds>=8.f && Sea.SwellPeriodSeconds<=13.f);
    const FPortVesselMotion Initial=Sea.SampleVesselMotion(0);
    TestFalse(TEXT("Enabled sea state produces vessel response"),Initial.TranslationCentimeters.IsNearlyZero() && Initial.Rotation.IsIdentity());
    bool Bounded=true,Changed=false,Finite=true;
    for(int32 I=0;I<20000;++I)
    {
        Sea.Advance(.5);
        const auto Motion=Sea.SampleVesselMotion(0);
        Bounded &= FMath::Abs(Motion.HeaveMeters)<=.2f && FMath::Abs(Motion.RollDegrees)<=.7f && FMath::Abs(Motion.PitchDegrees)<=.3f;
        Changed |= !Motion.TranslationCentimeters.Equals(Initial.TranslationCentimeters,.01f);
        Finite &= !Motion.TranslationCentimeters.ContainsNaN() && !Motion.LinearVelocityCentimetersPerSecond.ContainsNaN() &&
            !Motion.AngularVelocityRadiansPerSecond.ContainsNaN() && !Motion.Rotation.ContainsNaN();
    }
    TestTrue(TEXT("10000 simulated seconds retain sheltered response bounds"),Bounded);
    TestTrue(TEXT("Vessel motion evolves over time"),Changed);
    TestTrue(TEXT("Motion and analytic velocity remain finite"),Finite);
    FPortSeaState Fine,Coarse; Fine.Initialize(97531); Coarse.Initialize(97531);
    for(int32 I=0;I<16000;++I) Fine.Advance(.0625);
    for(int32 I=0;I<1000;++I) Coarse.Advance(1.);
    const auto FineMotion=Fine.SampleVesselMotion(1),CoarseMotion=Coarse.SampleVesselMotion(1);
    TestTrue(TEXT("Equal simulated time preserves translation"),FineMotion.TranslationCentimeters.Equals(CoarseMotion.TranslationCentimeters,.001f));
    TestTrue(TEXT("Equal simulated time preserves rotation"),FineMotion.Rotation.Equals(CoarseMotion.Rotation,.000001f));
    TestFalse(TEXT("Separate vessels do not move in lockstep"),Fine.SampleVesselMotion(0).TranslationCentimeters.Equals(FineMotion.TranslationCentimeters,.001f));
    FPortSeaState Kinematics; Kinematics.Initialize(-1617359246); Kinematics.ElapsedSeconds=237.5;
    constexpr double H=.01;
    FPortSeaState Before=Kinematics,After=Kinematics; Before.ElapsedSeconds-=H; After.ElapsedSeconds+=H;
    const FVector Pivot(-3400,0,0),Base(-4900,23300,827);
    const auto M0=Kinematics.SampleVesselMotion(2),Mm=Before.SampleVesselMotion(2),Mp=After.SampleVesselMotion(2);
    const FVector P0=M0.TransformPosition(Base,Pivot),Pm=Mm.TransformPosition(Base,Pivot),Pp=Mp.TransformPosition(Base,Pivot);
    const FVector DifferenceVelocity=(Pp-Pm)/(2*H);
    const FVector DifferenceAcceleration=(Pp-2*P0+Pm)/(H*H);
    const FVector DifferenceJerk=(Mp.AccelerationAtPosition(Base,Pivot)-Mm.AccelerationAtPosition(Base,Pivot))/(2*H);
    AddInfo(FString::Printf(TEXT("Vessel kinematics analytic_velocity=%s finite_velocity=%s analytic_acceleration=%s finite_acceleration=%s analytic_jerk=%s finite_jerk=%s"),
        *M0.VelocityAtPosition(Base,Pivot).ToCompactString(),*DifferenceVelocity.ToCompactString(),
        *M0.AccelerationAtPosition(Base,Pivot).ToCompactString(),*DifferenceAcceleration.ToCompactString(),
        *M0.JerkAtPosition(Base,Pivot).ToCompactString(),*DifferenceJerk.ToCompactString()));
    TestTrue(TEXT("Analytic vessel point velocity matches transformed-position finite difference"),
        M0.VelocityAtPosition(Base,Pivot).Equals(DifferenceVelocity,.05));
    TestTrue(TEXT("Analytic vessel point acceleration matches transformed-position finite difference"),
        M0.AccelerationAtPosition(Base,Pivot).Equals(DifferenceAcceleration,.2));
    TestTrue(TEXT("Analytic vessel point jerk matches acceleration finite difference"),
        M0.JerkAtPosition(Base,Pivot).Equals(DifferenceJerk,.5));
    const double FrozenAt=Sea.ElapsedSeconds;
    Sea.bSeaMotionEnabled=false;
    Sea.Advance(100.);
    const auto Off=Sea.SampleVesselMotion(0);
    TestEqual(TEXT("OFF freezes sea clock"),Sea.ElapsedSeconds,FrozenAt);
    TestTrue(TEXT("OFF returns neutral translation"),Off.TranslationCentimeters.IsZero());
    TestTrue(TEXT("OFF returns neutral rotation"),Off.Rotation.IsIdentity());
    Sea.bSeaMotionEnabled=true;
    Sea.Advance(1.);
    TestTrue(TEXT("ON resumes frozen sea clock"),Sea.ElapsedSeconds>FrozenAt);
    return true;
}
#endif
