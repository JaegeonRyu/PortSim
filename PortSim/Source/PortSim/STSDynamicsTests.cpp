#if WITH_DEV_AUTOMATION_TESTS
#include "STSDynamics.h"
#include "STSOperatingProfile.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSTSDynamicsTest,"PortSim.STS.SuspensionAndDrives",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSTSDynamicsTest::RunTest(const FString& Parameters)
{
    FSTSOperatingProfile P;
    if(!TestTrue(TEXT("Read full assumed dynamics profile"),P.Load(FPaths::ProjectDir()/TEXT("../Document/STS/STS_ReferenceData.json"),FPaths::ProjectConfigDir()/TEXT("STS_Simulation.json")))) return false;
    auto C=P.Dynamics; FSTSSuspension D;
    D.Step(C,.05,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);
    double Total=0;for(double T:D.Tension)Total+=T*C.Parts;
    TestTrue(TEXT("Static vertical rope forces balance suspended weight"),FMath::Abs(Total-17000*9.80665)<.01);
    const double Expected=17000*9.80665*C.DrumRadius/(C.Parts*C.GearRatio*C.Efficiency*C.MotorCount);
    TestTrue(TEXT("Per-motor static shaft torque includes ratio and efficiency"),FMath::Abs(D.MotorTorque-Expected)<.01);
    FSTSSuspension E;E.Step(C,.05,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector(.3,0,0),P.HoistPowerW);
    TestTrue(TEXT("Eccentric load changes individual line tensions"),E.Tension[1]>E.Tension[0]);
    FSTSSuspension Up;Up.Step(C,.05,20,-1,FVector(0,0,1),FVector(0,0,1),17000,FVector::ZeroVector,P.HoistPowerW);
    TestTrue(TEXT("Upward acceleration raises line tension and motor torque"),Up.Tension[0]>D.Tension[0] && Up.MotorTorque>D.MotorTorque);
    FSTSSuspension Horizontal;Horizontal.Step(C,.01,20,0,FVector(1,0,0),FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);
    TestTrue(TEXT("Below-plane mass transfers load toward positive acceleration"),Horizontal.Tension[1]>Horizontal.Tension[0]);
    FSTSSuspension On,Off;On.Yaw=Off.Yaw=.08;
    auto NoControl=C;NoControl.AntiSkew=false;
    for(int32 I=0;I<2400;++I) {On.Step(C,1./120.,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);Off.Step(NoControl,1./120.,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);}
    TestTrue(TEXT("Skew controller removes an initial yaw disturbance"),FMath::Abs(On.Yaw)<.001 && FMath::Abs(On.Yaw)+FMath::Abs(On.YawRate)<FMath::Abs(Off.Yaw)+FMath::Abs(Off.YawRate));
    auto RunSway=[&](bool Controlled,double H)
    {
        FSTSSuspension S;S.Angle.X=.05;double Velocity=0;
        for(int32 I=0;I<FMath::RoundToInt(30/H);++I)
        {
            const double A=Controlled?FMath::Clamp(3.*20*S.Rate.X,-1.5,1.5):0;
            Velocity+=A*H;
            S.Step(C,H,20,0,FVector(A,0,0),FVector(Velocity,0,0),17000,FVector::ZeroVector,P.HoistPowerW);
        }
        return S.Angle.SizeSquared()+S.Rate.SizeSquared();
    };
    TestTrue(TEXT("Positive trolley acceleration feedback damps sway"),RunSway(true,1./120.)<RunSway(false,1./120.));
    FSTSSuspension Fine,Coarse;Fine.Angle.X=Coarse.Angle.X=.05;
    for(int32 I=0;I<1200;++I)Fine.Step(C,1./120.,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);
    for(int32 I=0;I<200;++I)Coarse.Step(C,.05,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);
    TestTrue(TEXT("Fixed internal steps are consistent across outer tick rates"),FMath::Abs(Fine.Angle.X-Coarse.Angle.X)<.0001);
    for(double Length:{12.,25.,40.})
    {
        FSTSSuspension Travel; double X=0,Velocity=0,Peak=0,LastOutsideTolerance=0;
        for(int32 I=0;I<36000;++I)
        {
            constexpr double H=1./120.;
            const double Command=C.HorizontalAcceleration(40-X,Velocity,Length,Travel.Rate.X);
            const double Next=FMath::Clamp(Velocity+FMath::Clamp(Command,-.8,.8)*H,-4.,4.);
            const double A=(Next-Velocity)/H;Velocity=Next;X+=Velocity*H;
            Travel.Step(C,H,Length,0,FVector(A,0,0),FVector(Velocity,0,0),17000,FVector::ZeroVector,P.HoistPowerW);
            Peak=FMath::Max(Peak,Travel.SwayDegrees());
            const double SpreaderVelocity=Velocity+Length*FMath::Cos(Travel.Angle.X)*Travel.Rate.X;
            if(FMath::Abs(40-X-Travel.Offset.X)>=.05 || FMath::Abs(SpreaderVelocity)>=.05 || Travel.SwayDegrees()>=1)
                LastOutsideTolerance=(I+1)*H;
        }
        const double FinalSpreaderVelocity=Velocity+Length*FMath::Cos(Travel.Angle.X)*Travel.Rate.X;
        TestTrue(FString::Printf(TEXT("40m closed-loop travel settles at rope length %.0fm"),Length),FMath::Abs(40-X-Travel.Offset.X)<.05 && FMath::Abs(FinalSpreaderVelocity)<.05 && Peak<20 && Travel.Fault.IsEmpty());
        TestTrue(FString::Printf(TEXT("40m closed-loop travel remains settled after 90s at rope length %.0fm"),Length),LastOutsideTolerance<90);
    }
    auto WindConfig=C;WindConfig.Wind.X=5;FSTSSuspension WindState;
    for(int32 I=0;I<1200;++I)WindState.Step(WindConfig,1./120.,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);
    TestTrue(TEXT("Crosswind changes suspended position"),WindState.Offset.X>.001);
    const FVector WindEquilibrium=WindConfig.WindEquilibriumOffset(20,17000);
    TestTrue(TEXT("Wind feed-forward predicts the downwind equilibrium"),WindEquilibrium.X>.05 && WindEquilibrium.Y==0);
    auto HoldUnderWind=[&](bool Compensate)
    {
        FSTSSuspension S; double Trolley=0,Velocity=0;
        constexpr double H=1./120.,Length=20.,Mass=17000.;
        for(int32 I=0;I<7200;++I)
        {
            const double Target=Compensate?-WindConfig.WindEquilibriumOffset(Length,Mass).X:0.;
            const double Command=WindConfig.HorizontalAcceleration(Target-Trolley,Velocity,Length,S.Rate.X);
            const double Next=FMath::Clamp(Velocity+FMath::Clamp(Command,-1.5,1.5)*H,-4.,4.);
            const double Acceleration=(Next-Velocity)/H; Velocity=Next; Trolley+=Velocity*H;
            S.Step(WindConfig,H,Length,0,FVector(Acceleration,0,0),FVector(Velocity,0,0),Mass,FVector::ZeroVector,P.HoistPowerW);
        }
        return Trolley+S.Offset.X;
    };
    TestTrue(TEXT("Wind feed-forward holds the suspended load over its target"),
        FMath::Abs(HoldUnderWind(true))<.02 && FMath::Abs(HoldUnderWind(false))>.05);
    const double EquilibriumAngle=FMath::Asin(WindEquilibrium.X/20.);
    TestTrue(TEXT("Anti-sway angle feedback rejects lag from a changing wind equilibrium"),
        WindConfig.HorizontalAcceleration(0,0,20,0,EquilibriumAngle+.01,EquilibriumAngle,4)>0);
    TestTrue(TEXT("Anti-sway adds no angle correction at the measured wind equilibrium"),
        FMath::IsNearlyZero(WindConfig.HorizontalAcceleration(0,0,20,0,EquilibriumAngle,EquilibriumAngle,4)));
    struct FWindResponseMetrics
    {
        double PeakSwayErrorDegrees=0,PeakPositionErrorM=0,RmsSwayErrorDegrees=0,PeakAccelerationMps2=0;
    };
    auto WindResponse=[&](bool Gust,bool AntiSway)
    {
        auto Scenario=C; Scenario.AntiSway=AntiSway;
        FSTSSuspension S; double Trolley=0,Velocity=0,SquaredSwayError=0; int32 Samples=0;
        constexpr double H=1./120.,Length=20.,Mass=17000.,BaseWind=3.5,GustIncrement=4.,MeasureSeconds=60.;
        FWindResponseMetrics Metrics;
        auto Step=[&](double Time,bool Measure)
        {
            double GustSpeed=0;
            if(Gust && Time>=10 && Time<=20)
            {
                const double Phase=(Time-10)/10.;
                const double Envelope=Phase<.25?FMath::SmoothStep(0.,.25,Phase):
                    (Phase<=.65?1.:FMath::SmoothStep(1.,.65,Phase));
                GustSpeed=GustIncrement*Envelope;
            }
            Scenario.Wind=FVector(BaseWind+GustSpeed,0,0);
            const double Offset=Scenario.WindEquilibriumOffset(Length,Mass).X;
            const double Equilibrium=FMath::Asin(FMath::Clamp(Offset/Length,-1.,1.));
            const double Command=Scenario.HorizontalAcceleration(-Offset-Trolley,Velocity,Length,S.Rate.X,
                S.Angle.X,Equilibrium,4);
            const double Acceleration=FMath::Clamp(Command,-1.5,1.5);
            const double Next=FMath::Clamp(Velocity+Acceleration*H,-4.,4.);
            const double AppliedAcceleration=(Next-Velocity)/H; Velocity=Next; Trolley+=Velocity*H;
            S.Step(Scenario,H,Length,0,FVector(AppliedAcceleration,0,0),FVector(Velocity,0,0),Mass,FVector::ZeroVector,P.HoistPowerW);
            if(Measure)
            {
                const double SwayError=FMath::Abs(FMath::RadiansToDegrees(S.Angle.X-Equilibrium));
                Metrics.PeakSwayErrorDegrees=FMath::Max(Metrics.PeakSwayErrorDegrees,SwayError);
                Metrics.PeakPositionErrorM=FMath::Max(Metrics.PeakPositionErrorM,FMath::Abs(Trolley+S.Offset.X));
                Metrics.PeakAccelerationMps2=FMath::Max(Metrics.PeakAccelerationMps2,FMath::Abs(AppliedAcceleration));
                SquaredSwayError+=SwayError*SwayError; ++Samples;
            }
        };
        // Settle under the same mean wind before comparing the disturbance.
        for(int32 I=0;I<FMath::RoundToInt(60/H);++I) Step(-60+I*H,false);
        for(int32 I=0;I<FMath::RoundToInt(MeasureSeconds/H);++I) Step(I*H,true);
        Metrics.RmsSwayErrorDegrees=FMath::Sqrt(SquaredSwayError/FMath::Max(1,Samples));
        return Metrics;
    };
    const FWindResponseMetrics Normal=WindResponse(false,true);
    const FWindResponseMetrics GustControlled=WindResponse(true,true);
    const FWindResponseMetrics GustUncontrolled=WindResponse(true,false);
    const double NormalForce=.5*1.225*C.WindDrag*C.WindArea*3.5*3.5;
    const double GustForce=.5*1.225*C.WindDrag*C.WindArea*7.5*7.5;
    UE_LOG(LogTemp,Display,TEXT("PORTSIM_GUST_METRICS: mean_mps=3.5 peak_mps=7.5 force_ratio=%.3f normal_peak_sway_error_deg=%.6f normal_peak_position_error_m=%.6f anti_sway_peak_sway_error_deg=%.6f anti_sway_rms_sway_error_deg=%.6f anti_sway_peak_position_error_m=%.6f anti_sway_peak_acceleration_mps2=%.6f no_anti_sway_peak_sway_error_deg=%.6f no_anti_sway_rms_sway_error_deg=%.6f no_anti_sway_peak_position_error_m=%.6f"),
        GustForce/NormalForce,Normal.PeakSwayErrorDegrees,Normal.PeakPositionErrorM,
        GustControlled.PeakSwayErrorDegrees,GustControlled.RmsSwayErrorDegrees,GustControlled.PeakPositionErrorM,GustControlled.PeakAccelerationMps2,
        GustUncontrolled.PeakSwayErrorDegrees,GustUncontrolled.RmsSwayErrorDegrees,GustUncontrolled.PeakPositionErrorM);
    TestTrue(TEXT("Gust creates more sway error than steady mean wind"),GustControlled.PeakSwayErrorDegrees>Normal.PeakSwayErrorDegrees*2);
    TestTrue(TEXT("Anti-sway reduces gust peak sway error"),GustControlled.PeakSwayErrorDegrees<GustUncontrolled.PeakSwayErrorDegrees);
    TestTrue(TEXT("Anti-sway reduces gust RMS sway error"),GustControlled.RmsSwayErrorDegrees<GustUncontrolled.RmsSwayErrorDegrees);
    TestTrue(TEXT("Anti-sway reduces gust load-position error"),GustControlled.PeakPositionErrorM<GustUncontrolled.PeakPositionErrorM);
    auto LimitedMotor=C;LimitedMotor.MotorMaxTorque=1;FSTSSuspension MotorState;
    MotorState.Step(LimitedMotor,.05,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);
    TestTrue(TEXT("Motor saturation is exposed independently of rope limits"),MotorState.Saturated);
    auto LowLimit=C;LowLimit.RopeLimit=1;FSTSSuspension Fault;
    Fault.Step(LowLimit,.05,20,0,FVector::ZeroVector,FVector::ZeroVector,17000,FVector::ZeroVector,P.HoistPowerW);
    TestFalse(TEXT("Rope overload faults instead of masking forces"),Fault.Fault.IsEmpty());
    TestEqual(TEXT("All sensor families have mounted/ranged assumptions"),C.Mounts.Num(),14);
    const auto* Boom=C.Mounts.FindByPredicate([](const FSTSSensorMount& M){return M.Key==TEXT("boom_collision_lidar");});
    const auto* Encoder=C.Mounts.FindByPredicate([](const FSTSSensorMount& M){return M.Key==TEXT("trolley_encoder");});
    if(!TestNotNull(TEXT("AOS reference present"),Boom) || !TestNotNull(TEXT("KH53 reference present"),Encoder))return false;
    TestEqual(TEXT("AOS supports the manufacturer's scan wider than 180 degrees"),Boom->Fov,190.);
    TestEqual(TEXT("AOS nominal range remains distinct from 10 percent remission range"),Boom->Maximum,80.);
    TestEqual(TEXT("Two boom scanners use the gantry frame"),Boom->Frame,FString(TEXT("gantry")));
    TestEqual(TEXT("Second boom scanner is instantiated"),Boom->AdditionalPositions.Num(),1);
    TestTrue(TEXT("Boom scanner positions straddle the boom"),Boom->Position.Y*Boom->AdditionalPositions[0].Y<0);
    TestTrue(TEXT("KH53 reports 0.1mm increments relative to its configured zero"),FMath::Abs(Encoder->ReadPosition(Encoder->MeasurementOrigin+.00016)-.0002)<1.e-8);
    TestTrue(TEXT("Signed trolley coordinate maps to nonnegative device travel"),Encoder->ReadPosition(-50)>0);
    TestEqual(TEXT("KH53 speed validity limit"),Encoder->MaxTraversingSpeed,6.6);
    return true;
}
#endif
