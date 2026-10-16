// Copyright Epic Games, Inc. All Rights Reserved.

#include "Leak.h"
#include "Submarine.h"
#include "TheSubProjectCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/ConstructorHelpers.h"

ALeak::ALeak()
{
	// Ticks on the server to advance patching
	PrimaryActorTick.bCanEverTick = true;

	// Spawned by the server and attached to the sub; movement replication carries the attachment
	bReplicates = true;
	SetReplicatingMovement(true);
	bAlwaysRelevant = true;

	LeakRoot = CreateDefaultSubobject<USceneComponent>(TEXT("LeakRoot"));
	LeakRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = LeakRoot;

	// The leak sits on a wall with +X pointing into the room
	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(LeakRoot);
	Marker->SetMobility(EComponentMobility::Movable);
	Marker->SetRelativeScale3D(FVector(0.3f));
	Marker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Marker->SetCollisionResponseToAllChannels(ECR_Ignore);
	Marker->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Marker->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Marker->SetStaticMesh(SphereMesh.Object);
	}

	Spray = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("Spray"));
	Spray->SetupAttachment(LeakRoot);
	Spray->SetRelativeLocation(FVector(10.f, 0.f, 0.f));

	static ConstructorHelpers::FObjectFinder<UParticleSystem> SteamParticles(TEXT("/Game/StarterContent/Particles/P_Steam_Lit.P_Steam_Lit"));
	if (SteamParticles.Succeeded())
	{
		Spray->SetTemplate(SteamParticles.Object);
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(LeakRoot);
	Label->SetRelativeLocation(FVector(15.f, 0.f, 40.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(24.f);
	Label->SetTextRenderColor(FColor::Red);
	UpdateLabel();
}

void ALeak::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ALeak, PatchProgress);
}

void ALeak::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}

	// Drop anyone who left, died or wandered off
	const FVector LeakLocation = GetActorLocation();
	Patchers.RemoveAll([this, &LeakLocation](const TWeakObjectPtr<ATheSubProjectCharacter>& Patcher)
	{
		return !Patcher.IsValid() || FVector::DistSquared(Patcher->GetActorLocation(), LeakLocation) > FMath::Square(PatchDistance);
	});
	if (Patchers.Num() == 0)
	{
		return;
	}

	PatchProgress = FMath::Min(1.f, PatchProgress + Patchers.Num() * DeltaSeconds / PatchTime);
	UpdateLabel();

	if (PatchProgress >= 1.f)
	{
		UE_LOG(LogSubmarine, Log, TEXT("Leak patched by %s"), *GetNameSafe(Patchers[0].Get()));
		Destroy();
	}
}

FString ALeak::GetInteractionPrompt(const ATheSubProjectCharacter* Character) const
{
	return FString::Printf(TEXT("[Hold E] Patch leak (%.0f%%)"), PatchProgress * 100.f);
}

void ALeak::BeginInteract(ATheSubProjectCharacter* Character)
{
	if (Character != nullptr && FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) <= FMath::Square(PatchDistance))
	{
		Patchers.AddUnique(Character);
	}
}

void ALeak::EndInteract(ATheSubProjectCharacter* Character)
{
	Patchers.Remove(Character);
}

void ALeak::OnRep_PatchProgress()
{
	UpdateLabel();
}

void ALeak::UpdateLabel()
{
	Label->SetText(PatchProgress > 0.f
		? FText::FromString(FString::Printf(TEXT("LEAK %.0f%%"), PatchProgress * 100.f))
		: NSLOCTEXT("Leak", "Label", "LEAK"));
}
