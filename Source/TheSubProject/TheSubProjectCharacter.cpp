// Copyright Epic Games, Inc. All Rights Reserved.

#include "TheSubProjectCharacter.h"
#include "TheSubProjectProjectile.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Interactable.h"
#include "SubStation.h"
#include "Submarine.h"
#include "TheSubGameState.h"
#include "TheSubProjectGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

//////////////////////////////////////////////////////////////////////////
// ATheSubProjectCharacter

ATheSubProjectCharacter::ATheSubProjectCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
		
	// Create a CameraComponent	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(-10.f, 0.f, 60.f)); // Position the camera
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Create a mesh component that will be used when being viewed from a '1st person' view (when controlling this pawn)
	Mesh1P = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh1P"));
	Mesh1P->SetOnlyOwnerSee(true);
	Mesh1P->SetupAttachment(FirstPersonCameraComponent);
	Mesh1P->bCastDynamicShadow = false;
	Mesh1P->CastShadow = false;
	Mesh1P->SetRelativeLocation(FVector(-30.f, 0.f, -150.f));

}

//////////////////////////////////////////////////////////////////////////// Input

void ATheSubProjectCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);

			EnsureInteractInput();
			if (InteractMappingContext != nullptr)
			{
				Subsystem->AddMappingContext(InteractMappingContext, 0);
			}
		}
	}
}

void ATheSubProjectCharacter::EnsureInteractInput()
{
	// The defaults live on the local player, not on this character: a respawned character must reuse the same
	// objects, or the dead character's mapping context stays registered and can swallow E/R.
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	ULocalPlayer* LocalPlayer = PlayerController != nullptr ? PlayerController->GetLocalPlayer() : nullptr;
	if (LocalPlayer == nullptr)
	{
		return;
	}

	auto FindOrMakeAction = [LocalPlayer](const TCHAR* Name)
	{
		UInputAction* Action = FindObject<UInputAction>(LocalPlayer, Name);
		if (Action == nullptr)
		{
			Action = NewObject<UInputAction>(LocalPlayer, Name);
			Action->ValueType = EInputActionValueType::Boolean;
		}
		return Action;
	};

	if (InteractAction == nullptr)
	{
		InteractAction = FindOrMakeAction(TEXT("IA_Interact"));
	}
	if (RestartAction == nullptr)
	{
		RestartAction = FindOrMakeAction(TEXT("IA_Restart"));
	}
	if (InteractMappingContext == nullptr)
	{
		InteractMappingContext = FindObject<UInputMappingContext>(LocalPlayer, TEXT("IMC_Interact"));
		if (InteractMappingContext == nullptr)
		{
			InteractMappingContext = NewObject<UInputMappingContext>(LocalPlayer, TEXT("IMC_Interact"));
			InteractMappingContext->MapKey(InteractAction, EKeys::E);
			InteractMappingContext->MapKey(RestartAction, EKeys::R);
		}
	}
}

void ATheSubProjectCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATheSubProjectCharacter::Move);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATheSubProjectCharacter::Look);

		// Interacting
		EnsureInteractInput();
		if (InteractAction != nullptr && RestartAction != nullptr)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ATheSubProjectCharacter::Interact);
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Completed, this, &ATheSubProjectCharacter::StopInteract);
			EnhancedInputComponent->BindAction(RestartAction, ETriggerEvent::Started, this, &ATheSubProjectCharacter::RequestRestart);
		}
	}
	else
	{
		UE_LOG(LogTemplateCharacter, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void ATheSubProjectCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add movement 
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

void ATheSubProjectCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}
//////////////////////////////////////////////////////////////////////////// Stations

ASubStation* ATheSubProjectCharacter::GetCurrentStation() const
{
	return CurrentStation.Get();
}

void ATheSubProjectCharacter::SetCurrentStation(ASubStation* Station)
{
	CurrentStation = Station;
}

void ATheSubProjectCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// The server and the owning client both simulate this character's movement
	if (HasAuthority() || IsLocallyControlled())
	{
		UpdateWaterSlowdown();
	}

	if (!IsLocallyControlled())
	{
		return;
	}

	InteractionPrompt.Reset();
	if (CurrentStation.IsValid())
	{
		return;
	}

	AActor* Target = FindInteractableInView();

	// Holding interact (patching) only lasts while we keep looking at the same thing
	if (LocalHoldTarget.IsValid() && LocalHoldTarget.Get() != Target)
	{
		StopInteract();
	}

	if (const IInteractable* Interactable = Cast<IInteractable>(Target))
	{
		InteractionPrompt = Interactable->GetInteractionPrompt(this);
	}
}

void ATheSubProjectCharacter::BeginPlay()
{
	Super::BeginPlay();

	DryMaxWalkSpeed = GetCharacterMovement()->MaxWalkSpeed;
}

void ATheSubProjectCharacter::UpdateWaterSlowdown()
{
	// Only while standing inside a sub; in the air we keep whatever speed we had
	const UPrimitiveComponent* Base = GetMovementBase();
	const AActor* BaseActor = Base != nullptr ? Base->GetOwner() : nullptr;
	const ASubmarine* Submarine = Cast<ASubmarine>(BaseActor);
	if (Submarine == nullptr && BaseActor != nullptr)
	{
		Submarine = Cast<ASubmarine>(BaseActor->GetAttachParentActor());
	}
	if (Submarine == nullptr)
	{
		return;
	}

	// 0 = dry feet, 1 = water up to the top of the head
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float FeetZ = GetActorLocation().Z - HalfHeight;
	const float Submerged = FMath::Clamp((Submarine->GetWaterSurfaceZ() - FeetZ) / (HalfHeight * 2.f), 0.f, 1.f);
	const float Wading = Submarine->GetWaterLevel() > 1.f ? Submerged : 0.f;

	GetCharacterMovement()->MaxWalkSpeed = DryMaxWalkSpeed * FMath::Lerp(1.f, FullyFloodedSpeedScale, Wading);
}

void ATheSubProjectCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Don't leave a station occupied, or a leak "being patched", by someone who disconnected or died
	if (HasAuthority())
	{
		if (CurrentStation.IsValid())
		{
			CurrentStation->Leave(this);
		}
		EndHoldInteraction();
	}

	Super::EndPlay(EndPlayReason);
}

void ATheSubProjectCharacter::Interact()
{
	if (CurrentStation.IsValid())
	{
		ServerInteract(nullptr);
		return;
	}

	AActor* Target = FindInteractableInView();
	if (const IInteractable* Interactable = Cast<IInteractable>(Target))
	{
		if (Interactable->IsHoldInteraction())
		{
			LocalHoldTarget = Target;
		}
		ServerInteract(Target);
	}
}

void ATheSubProjectCharacter::StopInteract()
{
	if (LocalHoldTarget.IsValid())
	{
		bIsStationInUse = false;
		LocalHoldTarget.Reset();
		ServerStopInteract();
	}
}

void ATheSubProjectCharacter::ServerInteract_Implementation(AActor* Target)
{
	// Pressing interact while at a station always leaves it
	if (CurrentStation.IsValid())
	{
		CurrentStation->Leave(this);
		return;
	}

	EndHoldInteraction();

	IInteractable* Interactable = Cast<IInteractable>(Target);
	if (Interactable == nullptr)
	{
		return;
	}

	Interactable->BeginInteract(this);
	if (Interactable->IsHoldInteraction())
	{
		HoldTarget = Target;
	}
}

void ATheSubProjectCharacter::ServerStopInteract_Implementation()
{
	EndHoldInteraction();
}

void ATheSubProjectCharacter::RequestRestart()
{
	const ATheSubGameState* RoundState = GetWorld()->GetGameState<ATheSubGameState>();
	if (RoundState != nullptr && RoundState->IsRoundOver())
	{
		ServerRequestRestart();
	}
}

void ATheSubProjectCharacter::ServerRequestRestart_Implementation()
{
	if (ATheSubProjectGameMode* GameMode = GetWorld()->GetAuthGameMode<ATheSubProjectGameMode>())
	{
		GameMode->RestartRound();
	}
}

void ATheSubProjectCharacter::SubGoToBeacon()  { ServerDebugCommand(ESubDebugCommand::GoToBeacon); }
void ATheSubProjectCharacter::SubGoToSurface() { ServerDebugCommand(ESubDebugCommand::GoToSurface); }
void ATheSubProjectCharacter::SubWin()         { ServerDebugCommand(ESubDebugCommand::Win); }
void ATheSubProjectCharacter::SubFlood()       { ServerDebugCommand(ESubDebugCommand::Flood); }
void ATheSubProjectCharacter::SubLeak()        { ServerDebugCommand(ESubDebugCommand::Leak); }

void ATheSubProjectCharacter::ServerDebugCommand_Implementation(ESubDebugCommand Command)
{
	if (ATheSubProjectGameMode* GameMode = GetWorld()->GetAuthGameMode<ATheSubProjectGameMode>())
	{
		GameMode->RunDebugCommand(Command);
	}
}

void ATheSubProjectCharacter::EndHoldInteraction()
{
	if (IInteractable* Interactable = Cast<IInteractable>(HoldTarget.Get()))
	{
		Interactable->EndInteract(this);
	}
	HoldTarget.Reset();
}

AActor* ATheSubProjectCharacter::FindInteractableInView() const
{
	const FVector Start = FirstPersonCameraComponent->GetComponentLocation();
	const FVector End = Start + GetControlRotation().Vector() * InteractDistance;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractTrace), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		AActor* HitActor = Hit.GetActor();
		return Cast<IInteractable>(HitActor) != nullptr ? HitActor : nullptr;
	}
	return nullptr;
}
