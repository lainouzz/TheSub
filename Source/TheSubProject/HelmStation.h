// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SubStation.h"
#include "HelmStation.generated.h"

class UInputAction;

/**
 * Pilots the submarine.
 * W/S step the engine telegraph (full astern ... full ahead), A/D steer, Space/Ctrl ascend/descend.
 * The telegraph setting stays when the pilot leaves, so the sub keeps going without anyone at the helm.
 */
UCLASS()
class THESUBPROJECT_API AHelmStation : public ASubStation
{
	GENERATED_BODY()

public:
	AHelmStation();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FString GetStatusText() const override;

	/** Server: new round, telegraph back to all stop */
	virtual void Reset() override;

	/** Name of the current telegraph setting, e.g. "Half ahead" */
	UFUNCTION(BlueprintPure, Category="Helm")
	FString GetTelegraphName() const;

protected:
	/** Telegraph one step ahead (falls back to W / Up if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputAction* TelegraphUpAction;

	/** Telegraph one step astern (falls back to S / Down if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputAction* TelegraphDownAction;

	/** Hold to turn left (falls back to A if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputAction* RudderLeftAction;

	/** Hold to turn right (falls back to D if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputAction* RudderRightAction;

	/** Hold to ascend (falls back to Space if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputAction* DiveUpAction;

	/** Hold to descend (falls back to Left Ctrl if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputAction* DiveDownAction;

	virtual void OnEntered(ATheSubProjectCharacter* Character) override;
	virtual void OnLeft(ATheSubProjectCharacter* Character) override;
	virtual void BuildDefaultInput() override;
	virtual void BindStationInput(UEnhancedInputComponent& EnhancedInputComponent) override;
	virtual void OnLocalInputRemoved() override;

private:
	/** Two opposing held keys where the most recently pressed one wins (holding Space then Ctrl dives) */
	struct FHeldAxis
	{
		bool bNegativeHeld = false;
		bool bPositiveHeld = false;
		float LastPressed = 0.f;

		void Press(float Direction)
		{
			(Direction < 0.f ? bNegativeHeld : bPositiveHeld) = true;
			LastPressed = Direction;
		}

		void Release(float Direction)
		{
			(Direction < 0.f ? bNegativeHeld : bPositiveHeld) = false;
		}

		float GetValue() const
		{
			if (bNegativeHeld && bPositiveHeld)
			{
				return LastPressed;
			}
			return bPositiveHeld ? 1.f : (bNegativeHeld ? -1.f : 0.f);
		}
	};

	void TelegraphUp();
	void TelegraphDown();

	/** Binds Action so pressing/releasing it moves Axis in Direction, then sends the result */
	void BindHeldKey(UEnhancedInputComponent& EnhancedInputComponent, UInputAction* Action, FHeldAxis& Axis, float Direction);

	/** Sends rudder/dive to the server if they changed since last time */
	void SendHeldAxes();

	FHeldAxis RudderKeys;
	FHeldAxis DiveKeys;

	UFUNCTION(Server, Reliable)
	void ServerStepTelegraph(int8 Direction);

	UFUNCTION(Server, Reliable)
	void ServerSetRudder(float Value);

	UFUNCTION(Server, Reliable)
	void ServerSetDive(float Value);

	/** Server: push the telegraph setting to the sub */
	void ApplyTelegraph();

	/** Index into the telegraph notches; replicated so everyone can read it */
	UPROPERTY(Replicated)
	int32 TelegraphIndex;

	/** Last values this client sent, so we only send RPCs when something changes */
	float SentRudder = 0.f;
	float SentDive = 0.f;
};
