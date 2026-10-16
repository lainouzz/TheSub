// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SubStation.h"
#include "SonarStation.generated.h"

class UInputAction;
class UWidgetComponent;

/** One sonar ping: where the rays hit, and when (server time) */
USTRUCT()
struct FSonarPing
{
	GENERATED_BODY()

	/** World positions of everything the ping hit */
	UPROPERTY()
	TArray<FVector_NetQuantize> Contacts;

	/** World positions of beacons and the sub's SonarTestTargets that were in range */
	UPROPERTY()
	TArray<FVector_NetQuantize> Targets;

	/** Clearance from the bottom of the hull to the ground straight below, in cm; negative = nothing in range */
	UPROPERTY()
	float BelowDistance = -1.f;

	/** Clearance from the top of the hull to whatever is straight above, in cm; negative = nothing in range */
	UPROPERTY()
	float AboveDistance = -1.f;

	/** Server world time of the ping; negative = never pinged */
	UPROPERTY()
	double ServerTime = -1.0;
};

/**
 * Sonar console. The operator pings (Space); the server casts rays in a circle around the sub
 * and replicates the hits. The in-world screen on the console draws them as dots that appear as
 * the ping ring sweeps out and fade over time, so anyone in the room can read it.
 */
UCLASS()
class THESUBPROJECT_API ASonarStation : public ASubStation
{
	GENERATED_BODY()

	/** In-world sonar display */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Sonar, meta=(AllowPrivateAccess = "true"))
	UWidgetComponent* Screen;

public:
	ASonarStation();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual FString GetStatusText() const override;

	/** Server: new round, screen cleared */
	virtual void Reset() override;

	const FSonarPing& GetLastPing() const { return LastPing; }
	float GetRange() const { return Range; }
	float GetPingTravelTime() const { return PingTravelTime; }
	float GetContactFadeTime() const { return ContactFadeTime; }
	float GetGroundWarningDistance() const { return GroundWarningDistance; }

	/** Seconds until the next ping is allowed (0 = ready) */
	UFUNCTION(BlueprintPure, Category="Sonar")
	float GetCooldownRemaining() const;

	/** Seconds since the last ping, or a large number if there hasn't been one */
	float GetTimeSinceLastPing() const;

protected:
	/** How far the ping reaches, in cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sonar", meta=(ClampMin="100"))
	float Range = 5000.f;

	/** Rays per ring, spread evenly around the sub */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sonar", meta=(ClampMin="4", ClampMax="720"))
	int32 NumRays = 120;

	/** One ring of rays per entry, tilted by this many degrees (0 = level, negative = down at the ground) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sonar")
	TArray<float> RayPitches = { 0.f, -30.f };

	/** Within this clearance (cm) below the hull the ground reading turns red */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sonar", meta=(ClampMin="0"))
	float GroundWarningDistance = 500.f;

	/** Seconds between pings */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sonar", meta=(ClampMin="0"))
	float Cooldown = 3.f;

	/** Seconds for the ping ring to sweep out to full range on the screen */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sonar", meta=(ClampMin="0.05"))
	float PingTravelTime = 1.f;

	/** Seconds until contacts fade out completely */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sonar", meta=(ClampMin="0.1"))
	float ContactFadeTime = 8.f;

	/** Ping (falls back to Space / left mouse if not set) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Station|Input")
	UInputAction* PingAction;

	virtual void BuildDefaultInput() override;
	virtual void BindStationInput(UEnhancedInputComponent& EnhancedInputComponent) override;

private:
	void Ping();

	UFUNCTION(Server, Reliable)
	void ServerPing();

	double GetServerTime() const;

	/** Latest ping; replicated as one struct so contacts and timestamp always arrive together */
	UPROPERTY(Replicated)
	FSonarPing LastPing;
};
