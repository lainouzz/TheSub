// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubStation.h"
#include "Submarine.h"
#include "TheSubProjectCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ASubStation::ASubStation()
{
	PrimaryActorTick.bCanEverTick = false;

	// Spawned by the server and attached to the sub. Movement replication must be on for the
	// attachment to replicate; while attached only the relative offset is sent.
	bReplicates = true;
	SetReplicatingMovement(true);
	bAlwaysRelevant = true;

	StationRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StationRoot"));
	StationRoot->SetMobility(EComponentMobility::Movable);
	RootComponent = StationRoot;

	// Console sits at the origin; the operator stands behind it (-X) facing forward (+X)
	ConsoleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ConsoleMesh"));
	ConsoleMesh->SetupAttachment(StationRoot);
	ConsoleMesh->SetMobility(EComponentMobility::Movable);
	ConsoleMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	ConsoleMesh->SetCanEverAffectNavigation(false);
	ConsoleMesh->SetRelativeLocation(FVector(0.f, 0.f, 55.f));
	ConsoleMesh->SetRelativeScale3D(FVector(0.6f, 1.4f, 1.1f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		ConsoleMesh->SetStaticMesh(CubeMesh.Object);
	}

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(StationRoot);
	NameLabel->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	// Text faces +X; turn it around so the operator standing behind the console can read it
	NameLabel->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetWorldSize(30.f);
	NameLabel->SetTextRenderColor(FColor::White);

	StationName = NSLOCTEXT("SubStation", "DefaultName", "Station");
}

void ASubStation::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	UpdateLabel();
}

FString ASubStation::GetInteractionPrompt(const ATheSubProjectCharacter* Character) const
{
	return Occupant != nullptr
		? FString::Printf(TEXT("%s (in use by %s)"), *StationName.ToString(), *GetOccupantName())
		: FString::Printf(TEXT("[E] Use %s"), *StationName.ToString());
}

FString ASubStation::GetOccupantName() const
{
	if (Occupant == nullptr)
	{
		return FString();
	}

	// The PlayerState can arrive a moment after the occupant on clients
	const APlayerState* PlayerState = Occupant->GetPlayerState();
	return PlayerState != nullptr ? PlayerState->GetPlayerName() : TEXT("someone");
}

void ASubStation::UpdateLabel()
{
	// TextRender uses <br> for line breaks
	const FString Label = Occupant != nullptr
		? FString::Printf(TEXT("%s<br>%s"), *StationName.ToString(), *GetOccupantName())
		: StationName.ToString();
	NameLabel->SetText(FText::FromString(Label));
}

void ASubStation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && Occupant != nullptr)
	{
		Leave(Occupant);
	}
	RemoveLocalInput();

	Super::EndPlay(EndPlayReason);
}

void ASubStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASubStation, Occupant);
}

void ASubStation::Reset()
{
	Super::Reset();

	if (HasAuthority() && Occupant != nullptr)
	{
		Leave(Occupant);
	}
}

ASubmarine* ASubStation::GetSubmarine() const
{
	for (AActor* Parent = GetAttachParentActor(); Parent != nullptr; Parent = Parent->GetAttachParentActor())
	{
		if (ASubmarine* Submarine = Cast<ASubmarine>(Parent))
		{
			return Submarine;
		}
	}
	return nullptr;
}

bool ASubStation::CanEnter(const ATheSubProjectCharacter* Character) const
{
	return Character != nullptr
		&& Occupant == nullptr
		&& Character->GetCurrentStation() == nullptr
		&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) <= FMath::Square(UseDistance);
}

void ASubStation::Enter(ATheSubProjectCharacter* Character)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!CanEnter(Character))
	{
		UE_LOG(LogSubmarine, Log, TEXT("%s can't use %s (occupant: %s, already at: %s, distance %.0f / %.0f)"),
			*GetNameSafe(Character), *StationName.ToString(), *GetNameSafe(Occupant),
			Character ? *GetNameSafe(Character->GetCurrentStation()) : TEXT("-"),
			Character ? FVector::Dist(Character->GetActorLocation(), GetActorLocation()) : 0.f, UseDistance);
		return;
	}

	UE_LOG(LogSubmarine, Log, TEXT("%s took %s"), *GetNameSafe(Character), *StationName.ToString());

	// Owning the station lets the occupant's client call its server RPCs
	SetOwner(Character);
	SetOccupant(Character);
	OnEntered(Character);
}

void ASubStation::Leave(ATheSubProjectCharacter* Character)
{
	if (!HasAuthority() || Character == nullptr || Character != Occupant)
	{
		return;
	}

	UE_LOG(LogSubmarine, Log, TEXT("%s left %s"), *GetNameSafe(Character), *StationName.ToString());

	OnLeft(Character);
	SetOccupant(nullptr);
	SetOwner(nullptr);
}

void ASubStation::SetOccupant(ATheSubProjectCharacter* NewOccupant)
{
	ATheSubProjectCharacter* OldOccupant = Occupant;
	Occupant = NewOccupant;

	// The server (listen host) doesn't get rep notifies, so run the same logic by hand
	OnRep_Occupant(OldOccupant);
}

void ASubStation::OnRep_Occupant(ATheSubProjectCharacter* OldOccupant)
{
	if (OldOccupant != nullptr && OldOccupant->GetCurrentStation() == this)
	{
		OldOccupant->SetCurrentStation(nullptr);
	}
	if (Occupant != nullptr)
	{
		Occupant->SetCurrentStation(this);
	}

	UpdateLabel();

	if (LocalInputCharacter.IsValid() && LocalInputCharacter.Get() != Occupant)
	{
		RemoveLocalInput();
	}
	if (Occupant != nullptr && Occupant->IsLocallyControlled() && LocalInputCharacter.Get() != Occupant)
	{
		AddLocalInput(Occupant);
	}

	UE_LOG(LogSubmarine, Log, TEXT("[%s] %s occupant %s -> %s (local input: %s)"),
		HasAuthority() ? TEXT("Server") : TEXT("Client"), *StationName.ToString(),
		*GetNameSafe(OldOccupant), *GetNameSafe(Occupant), *GetNameSafe(LocalInputCharacter.Get()));
}

void ASubStation::AddLocalInput(ATheSubProjectCharacter* Character)
{
	BuildDefaultInput();

	if (APlayerController* PlayerController = Cast<APlayerController>(Character->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (StationMappingContext != nullptr)
			{
				Subsystem->AddMappingContext(StationMappingContext, StationInputPriority);
			}
		}
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(Character->InputComponent))
	{
		BindStationInput(*EnhancedInputComponent);
	}

	LocalInputCharacter = Character;
}

void ASubStation::RemoveLocalInput()
{
	ATheSubProjectCharacter* Character = LocalInputCharacter.Get();
	LocalInputCharacter.Reset();
	if (Character == nullptr)
	{
		StationBindingHandles.Reset();
		return;
	}

	if (APlayerController* PlayerController = Cast<APlayerController>(Character->GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (StationMappingContext != nullptr)
			{
				Subsystem->RemoveMappingContext(StationMappingContext);
			}
		}
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(Character->InputComponent))
	{
		for (const uint32 Handle : StationBindingHandles)
		{
			EnhancedInputComponent->RemoveBindingByHandle(Handle);
		}
	}
	StationBindingHandles.Reset();

	OnLocalInputRemoved();
}
