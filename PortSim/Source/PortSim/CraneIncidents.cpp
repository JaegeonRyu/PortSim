#include "PortWorkingCrane.h"
#include "PortAGVActor.h"

void APortWorkingCrane::SetEquipmentFailure(bool Failed)
{
    if(Failed)
    {
        if(Fault.IsEmpty()||IsEquipmentUnavailable()){IncidentResponses.Add(TEXT("equipment_failure"));Stop(TEXT("Equipment controller unavailable; retention brakes applied"),EPortCraneFault::EquipmentUnavailable);}
    }
    else if(FaultType==EPortCraneFault::EquipmentUnavailable)
    {
        Fault.Empty();FaultType=EPortCraneFault::None;
        if(bGantryFault)Stop(TEXT("Gantry drive unavailable"),EPortCraneFault::GantryUnavailable);
        else if(bSensorFault)Stop(TEXT("Sensor observation unavailable"),EPortCraneFault::SensorUnavailable);
    }
}

bool APortWorkingCrane::IncidentReleaseWindow() const
{
    return bJobActive&&Stage==5&&bCarrying&&Observation.bCargoSupported&&
        Observation.SpreaderVelocity.Size()<=STSProfile.SettleSpeed&&
        Observation.CargoPosition.Equals(Slots[1-SourceSlot],STSProfile.LandingTolerance);
}

bool APortWorkingCrane::IncidentHandoverWindow() const
{
    return bJobActive&&((bSTS&&Stage==5&&bDestinationReady)||(!bSTS&&Stage<=2&&IsValid(HandoverAGV)));
}
