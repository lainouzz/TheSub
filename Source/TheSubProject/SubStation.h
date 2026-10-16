// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactable.h"
#include "SubStation.generated.h"

class ASubmarine;
class ATheSubProjectCharacter;
class UEnhancedInputComponent;
class UInputMappingContext;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * A console inside the submarine that one player at a time can operate (helm, sonar, ...).
 * The server decides who the occupant is and makes them the station's network owner,
 * so the occupant's client can send the station's server RPCs.
 * While a local player occupies it, the station adds its own input mapping context on top
 * of the character's, which blocks walking on the keys the station uses.
 */
UCLASS(Abstract)
class THESUBPROJECT_API ASubStation : public AActor, public IInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Station, meta=(AllowPrivateAccess = "true"))
	USceneComponent* StationRoot;

	/** Greybox console; this is what players look at to interact */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Station, meta=(AllowPrivateAccess = "true"))
	UStaticMeshComponent* ConsoleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Station, meta=(AllowPrivateAccess = "true"))
	UTextRenderComponent* NameLabel;

public:
	ASubStation();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: new round, nobody at the station */
	virtual void Reset() override;

	// IInteractable: pressing E takes the station (leaving is handled by the character)
	virtual FString GetInteractionPrompt(const ATheSubProjectCharacter* Character) const override;
	virtual void BeginInteract(ATheSubProjectCharacter* Character) override { Enter(Character); }

	/** Server: can this character take the station right now? */
	bool CanEnter(const ATheSubProjectCharacter* Character) const;

	/** Server: make Character the occupant */
	void Enter(ATheSubProjectCharacter* Character);

	/** Server: Character stops operating the station */
	void Leave(ATheSubProjectCharacter* Character);

	UFUNCTION(BlueprintPure, Category="Station")
	ATheSubProjectCharacter* GetOccupant() const { return Occupant; }

	UFUNCTION(BlueprintPure, Category="Station")
	FText GetStationName() const { return StationName; }

	/** Player name of whoever is at the station, empty if nobody */
	UFUNCTION(BlueprintPure, Category="Station")
	FString GetOccupantName() const;

	/** The submarine this station is attached to, if any */
	UFUNCTION(BlueprintPure, Category="Station")
	ASubmarine* GetSubmarine() const;

	/** Lines shown on the occupant's HUD while operating the station */
	virtual FString GetStatusText() const { return FString(); }

protected:
	/** Shown on the label and in interaction prompts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station")
	FText StationName;

	/** How far (cm) a character can be from the station and still take it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station")
	float UseDistance = 300.f;

	/** Mapping context added for the occupant. If left empty, the station builds a default one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputMappingContext* StationMappingContext;

	/** Higher than the character's default context so station keys take over */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	int32 StationInputPriority = 1;

	/** Server: someone just took the station */
	virtual void OnEntered(ATheSubProjectCharacter* Character) {}

	/** Server: the occupant just left */
	virtual void OnLeft(ATheSubProjectCharacter* Character) {}

	/** Local occupant: create default input actions / StationMappingContext if none were assigned */
	virtual void BuildDefaultInput() {}

	/** Local occupant: bind station actions; add each binding's handle to StationBindingHandles */
	virtual void BindStationInput(UEnhancedInputComponent& EnhancedInputComponent) {}

	/** Local occupant: station input was just removed */
	virtual void OnLocalInputRemoved() {}

	TArray<uint32> StationBindingHandles;

	UTextRenderComponent* GetNameLabel() const { return NameLabel; }

private:
	/** Label shows the station name, plus who's using it */
	void UpdateLabel();

	void SetOccupant(ATheSubProjectCharacter* NewOccupant);

	UFUNCTION()
	void OnRep_Occupant(ATheSubProjectCharacter* OldOccupant);

	void AddLocalInput(ATheSubProjectCharacter* Character);
	void RemoveLocalInput();

	UPROPERTY(ReplicatedUsing=OnRep_Occupant)
	ATheSubProjectCharacter* Occupant;

	/** The local character whose input we hooked into, so we can unhook it again */
	TWeakObjectPtr<ATheSubProjectCharacter> LocalInputCharacter;
};
