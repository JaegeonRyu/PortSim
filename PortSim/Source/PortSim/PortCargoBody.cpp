#include "PortCargoBody.h"
#include "PortContainerActor.h"
#include "PortSiteLogistics.h"

void UPortCargoBody::SetSimulatePhysics(bool Enabled)
{
    Super::SetSimulatePhysics(Enabled);
    if(auto* Cargo=Cast<APortContainerActor>(GetOwner()))
        if(auto* Site=Cast<APortSiteLogistics>(Cargo->GetOwner())) Site->RefreshCargoActivity(Cargo);
}

void UPortCargoBody::OnAttachmentChanged()
{
    Super::OnAttachmentChanged();
    if(auto* Cargo=Cast<APortContainerActor>(GetOwner()))
        if(auto* Site=Cast<APortSiteLogistics>(Cargo->GetOwner())) Site->RefreshCargoActivity(Cargo);
}
