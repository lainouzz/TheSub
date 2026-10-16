// Copyright Epic Games, Inc. All Rights Reserved.

#include "Beacon.h"
#include "Submarine.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ABeacon::ABeacon()
{
	// Ticks on the server to check for the sub
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;

	bReplicates = true;
	bAlwaysRelevant = true;

	// Stands on the trench floor: a 1m x 3m pillar
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetRelativeScale3D(FVector(1.f, 1.f, 3.f));
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}

	// Children of the scaled pillar; positions are in its scaled space
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
	Light->SetIntensity(50000.f);
	Light->SetAttenuationRadius(4000.f);
	Light->SetCastShadows(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Mesh);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
	Label->SetAbsolute(false, false, true);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(150.f);

	UpdateLook();
}

void ABeacon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABeacon, bReached);
}

void ABeacon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority() || bReached)
	{
		return;
	}

	for (TActorIterator<ASubmarine> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) <= FMath::Square(ReachRadius))
		{
			bReached = true;
			UpdateLook();
			UE_LOG(LogSubmarine, Log, TEXT("Beacon reached! Now get back to the surface."));
			break;
		}
	}
}

void ABeacon::Reset()
{
	Super::Reset();

	bReached = false;
	UpdateLook();
}

void ABeacon::DebugMarkReached()
{
	if (HasAuthority())
	{
		bReached = true;
		UpdateLook();
	}
}

void ABeacon::OnRep_Reached()
{
	UpdateLook();
}

void ABeacon::UpdateLook()
{
	Light->SetLightColor(bReached ? FLinearColor::Green : FLinearColor(1.f, 0.6f, 0.05f));
	Label->SetTextRenderColor(bReached ? FColor::Green : FColor::Orange);
	Label->SetText(bReached ? NSLOCTEXT("Beacon", "Reached", "BEACON REACHED") : NSLOCTEXT("Beacon", "Label", "BEACON"));
}
