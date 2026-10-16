// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

class ATheSubProjectCharacter;

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything a player can aim at and use with the interact key (E): stations, leaks, ...
 * Press interactions (stations) only get BeginInteract. Hold interactions (patching a leak) also get
 * EndInteract when the key is released, the player looks away, or the interaction otherwise stops.
 * Begin/EndInteract run on the server; the actor must be replicated so clients can name it in RPCs.
 */
class THESUBPROJECT_API IInteractable
{
	GENERATED_BODY()

public:
	/** Prompt shown to a local player looking at this, e.g. "[E] Use Helm". Empty = nothing to do. */
	virtual FString GetInteractionPrompt(const ATheSubProjectCharacter* Character) const = 0;

	/** Server: Character pressed interact while looking at this */
	virtual void BeginInteract(ATheSubProjectCharacter* Character) = 0;

	/** Server: Character stopped a hold interaction (only called when IsHoldInteraction) */
	virtual void EndInteract(ATheSubProjectCharacter* Character) {}

	/** True if the interaction lasts while the key is held instead of happening once on press */
	virtual bool IsHoldInteraction() const { return false; }
};
