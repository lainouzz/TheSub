// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "TheSubProjectGameMode.h"
#include "TheSubProjectCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class ASubStation;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

UCLASS(config=Game)
class ATheSubProjectCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: 1st person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Mesh, meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* Mesh1P;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** MappingContext */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	UInputMappingContext* DefaultMappingContext;

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta = (AllowPrivateAccess = "true"))
	class UInputAction* LookAction;

	/** Interact Input Action: use / leave stations, hold to patch leaks (falls back to E if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* InteractAction;

	/** Start a new round once the current one is over (falls back to R if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputAction* RestartAction;

	/** Mapping context for InteractAction and RestartAction (built automatically if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Input, meta=(AllowPrivateAccess = "true"))
	UInputMappingContext* InteractMappingContext;

	/** How far (cm) from the camera you can reach a station */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Interaction, meta=(AllowPrivateAccess = "true"))
	float InteractDistance = 250.f;

	/** Walk speed multiplier when the water inside the sub is up to your head (scales down from 1 as it rises) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Movement, meta=(AllowPrivateAccess = "true", ClampMin="0", ClampMax="1"))
	float FullyFloodedSpeedScale = 0.35f;

public:
	ATheSubProjectCharacter();

	virtual void Tick(float DeltaSeconds) override;

	/** Station this character is operating, if any (kept in sync by the station on every machine) */
	ASubStation* GetCurrentStation() const;
	void SetCurrentStation(ASubStation* Station);

	/** "[E] Use Helm" etc. for the local player's HUD; empty when there's nothing to interact with */
	const FString& GetInteractionPrompt() const { return InteractionPrompt; }

	// Testing shortcuts: type these in the console (~). They run on the server; disabled in Shipping builds.

	/** Teleport the sub (and everyone inside) next to the beacon */
	UFUNCTION(Exec)
	void SubGoToBeacon();

	/** Teleport the sub back to its start, at the surface */
	UFUNCTION(Exec)
	void SubGoToSurface();

	/** Mark the beacon reached and surface: instant win */
	UFUNCTION(Exec)
	void SubWin();

	/** Fill the sub with water: instant loss */
	UFUNCTION(Exec)
	void SubFlood();

	/** Spring a leak on a random wall */
	UFUNCTION(Exec)
	void SubLeak();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Slow down while wading through water inside the sub (server and owning client must agree) */
	void UpdateWaterSlowdown();

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

	/** Interact pressed: leave our station, or start using whatever we're looking at */
	void Interact();

	/** Interact released: stops a hold interaction (patching) */
	void StopInteract();

	/** Target is what we're looking at; null means "leave my station" */
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerStopInteract();

	/** Restart key: asks the server for a new round (only does anything once the round is over) */
	void RequestRestart();

	UFUNCTION(Server, Reliable)
	void ServerRequestRestart();

	UFUNCTION(Server, Reliable)
	void ServerDebugCommand(ESubDebugCommand Command);

	/** Interactable actor under the crosshair within reach, if any */
	AActor* FindInteractableInView() const;

private:
	/** Server: end the hold interaction we're doing, if any */
	void EndHoldInteraction();

	/** Server: what we're currently holding interact on */
	TWeakObjectPtr<AActor> HoldTarget;

	/** Owning client: what we started holding interact on, so we can stop when we look away */
	TWeakObjectPtr<AActor> LocalHoldTarget;

	/** Creates InteractAction / InteractMappingContext if they weren't assigned */
	void EnsureInteractInput();

	TWeakObjectPtr<ASubStation> CurrentStation;

	/** MaxWalkSpeed before any water slowdown */
	float DryMaxWalkSpeed = 0.f;

	FString InteractionPrompt;

protected:
	// APawn interface
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	// End of APawn interface

public:
	/** Returns Mesh1P subobject **/
	USkeletalMeshComponent* GetMesh1P() const { return Mesh1P; }
	/** Returns FirstPersonCameraComponent subobject **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

};

