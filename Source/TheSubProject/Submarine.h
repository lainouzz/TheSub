// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Submarine.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UPointLightComponent;
class USpotLightComponent;
class UArrowComponent;
class ASubStation;
class ALeak;
class UAudioComponent;
class USoundBase;
class USoundWaveProcedural;
class USceneCaptureComponent2D;

THESUBPROJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogSubmarine, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSubmarineImpact, float, ImpactSpeed, const FHitResult&, Hit);

/** Snapshot of the server's simulation, sent to clients so they can run the same simulation locally */
USTRUCT()
struct FSubmarineMoveState
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;

	UPROPERTY()
	float Speed = 0.f;

	UPROPERTY()
	float TurnRate = 0.f;

	UPROPERTY()
	float DiveSpeed = 0.f;

	UPROPERTY()
	float Throttle = 0.f;

	UPROPERTY()
	float Rudder = 0.f;

	UPROPERTY()
	float Dive = 0.f;

	/** Server world time when this snapshot was taken */
	UPROPERTY()
	double ServerTime = 0.0;
};

/** One step of the test autopilot: hold these inputs for Duration seconds */
USTRUCT(BlueprintType)
struct FSubmarineTestStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine")
	FName Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine", meta=(ClampMin="-1", ClampMax="1"))
	float Throttle = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine", meta=(ClampMin="-1", ClampMax="1"))
	float Rudder = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine", meta=(ClampMin="-1", ClampMax="1"))
	float Dive = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine", meta=(ClampMin="0.1"))
	float Duration = 5.f;
};

/**
 * A kinematic, server-owned submarine players walk around inside.
 * The server simulates movement from smoothed throttle / rudder / dive inputs and replicates a
 * snapshot. Clients run the same simulation locally, extrapolate each snapshot by the lag and
 * blend out the error, so the hull glides instead of snapping. Characters standing on the floor
 * ride along through CharacterMovement's based movement.
 */
UCLASS(config=Game)
class THESUBPROJECT_API ASubmarine : public AActor
{
	GENERATED_BODY()

	/** Outer hull collision; this is what sweeps against the world and bumps into walls */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UBoxComponent* HullCollision;

	/** Origin of the interior, sitting on top of the floor */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	USceneComponent* InteriorRoot;

	/** Optional hull mesh (assign your own submarine mesh in a Blueprint child) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UStaticMeshComponent* HullMesh;
	
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> HelmScreenMesh;

	/** Greybox floor, ceiling, outer walls and room dividers, sized by InteriorSize */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	TArray<UStaticMeshComponent*> InteriorPieces;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UPointLightComponent* HelmLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UPointLightComponent* SonarLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UPointLightComponent* EngineLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (AllowPrivateAccess = "true"))
	UPointLightComponent* HallRoofwayLight_1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (AllowPrivateAccess = "true"))
	UPointLightComponent* HallRoofwayLight_2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (AllowPrivateAccess = "true"))
	UPointLightComponent* HallwayRedLight_1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (AllowPrivateAccess = "true"))
	UPointLightComponent* HallwayRedLight_2;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (AllowPrivateAccess = "true"))
	UPointLightComponent* HallwayRedLight_3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (AllowPrivateAccess = "true"))
	UPointLightComponent* HallwayRedLight_4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (AllowPrivateAccess = "true"))
	USpotLightComponent* NoseSpotLight;

	/** Siren that wails while there are leaks */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UAudioComponent* AlarmAudio;

	/** Where players spawn inside the sub (move it in the Blueprint/viewport) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	USceneComponent* PlayerSpawnPoint;

	/** Where the helm console is spawned (helm room, against the bow wall) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	USceneComponent* HelmStationPoint;

	/** Where the sonar console is spawned (middle room, against the port wall) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	USceneComponent* SonarStationPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	USceneComponent* ElectricStationPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta = (AllowPrivateAccess = "true"))
	USceneComponent* EngineStationPoint;

	/** Water surface inside the sub; sits at WaterLevel above the floor, hidden when dry */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UStaticMeshComponent* WaterSurface;

	/** Shows which way is forward in the editor */
	UPROPERTY(VisibleAnywhere, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UArrowComponent* ForwardArrow;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Submarine, meta = (allowPrivateAccess = "true"))
	USceneCaptureComponent2D* HelmScreenComponent;

public:
	ASubmarine();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Forward/backward drive, -1..1. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Submarine")
	void SetThrottle(float Value);

	/** Turn left/right, -1..1. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Submarine")
	void SetRudder(float Value);

	/** Ascend (+) / descend (-), -1..1. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Submarine")
	void SetDive(float Value);

	/** Bring the sub to a smooth stop. Server only. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Submarine")
	void AllStop();

	UFUNCTION(BlueprintPure, Category="Submarine")
	float GetCurrentSpeed() const { return CurrentSpeed; }

	UFUNCTION(BlueprintPure, Category="Submarine")
	float GetThrottle() const { return Throttle; }

	UFUNCTION(BlueprintPure, Category="Submarine")
	float GetRudder() const { return Rudder; }

	UFUNCTION(BlueprintPure, Category="Submarine")
	float GetDive() const { return Dive; }

	/** Server: pause the test autopilot while someone pilots by hand. Resuming starts the loop over. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Submarine")
	void SetAutopilotSuspended(bool bSuspended) { bAutopilotSuspended = bSuspended; }

	const TArray<AActor*>& GetSonarTestTargets() const { return SonarTestTargets; }

	/** Water depth inside the sub, in cm above the floor */
	UFUNCTION(BlueprintPure, Category="Submarine|Water")
	float GetWaterLevel() const { return WaterLevel; }

	UFUNCTION(BlueprintPure, Category="Submarine|Water")
	float GetMaxWaterLevel() const { return MaxWaterLevel; }

	/** World Z of the water surface inside the sub */
	UFUNCTION(BlueprintPure, Category="Submarine|Water")
	float GetWaterSurfaceZ() const;

	UFUNCTION(BlueprintPure, Category="Submarine|Water")
	int32 GetNumLeaks() const { return NumLeaks; }

	/** How far below the surface the sub is, in cm (negative = above it) */
	UFUNCTION(BlueprintPure, Category="Submarine|Depth")
	float GetDepth() const { return SurfaceZ - GetActorLocation().Z; }

	/** World Z of the sea surface */
	UFUNCTION(BlueprintPure, Category="Submarine|Depth")
	float GetSurfaceZ() const { return SurfaceZ; }

	/** Distance from the hull's center to its top/bottom, in cm */
	UFUNCTION(BlueprintPure, Category="Submarine")
	float GetHullHalfHeight() const;

	/** Server: stop the sub and ignore all steering (round over). Cleared by Reset. */
	void SetControlsLocked(bool bLocked);

	/** Server: back to the start of a round - start position, dry, no leaks, stopped */
	virtual void Reset() override;

	/** Server, debug: jump to Location/Rotation (everyone standing inside comes along), stopped */
	void DebugTeleport(const FVector& Location, const FRotator& Rotation);

	/** Server, debug: fill with water (loses the round) */
	void DebugFlood();

	/** Server, debug: spring a leak on a random wall */
	void DebugSpawnLeak();

	/** Server: where the sub started this round */
	const FTransform& GetStartTransform() const { return StartTransform; }

	/** True once the water reached MaxWaterLevel: the crew lost */
	UFUNCTION(BlueprintPure, Category="Submarine|Water")
	bool IsFlooded() const { return bFlooded; }

	/** Server world time, estimated on clients; use it to compare against replicated server timestamps */
	double GetServerTime() const;

	/** World transform the player should spawn at */
	UFUNCTION(BlueprintPure, Category="Submarine")
	FTransform GetPlayerSpawnTransform() const;

	/** Fires on the server when the hull hits something hard enough (hook hull damage in here later) */
	UPROPERTY(BlueprintAssignable, Category="Submarine")
	FOnSubmarineImpact OnHullImpact;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Throttle the sub starts with (0 = parked, 0.3 = slow cruise) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="-1", ClampMax="1"))
	float InitialThrottle = 0.f;

	/** Top forward speed in cm/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="0"))
	float MaxSpeed = 400.f;

	/** How quickly speed catches up to the throttle (higher = snappier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="0"))
	float Acceleration = 0.5f;

	/** Max turn rate in degrees/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="0"))
	float MaxTurnRate = 10.f;

	/** How quickly the turn rate catches up to the rudder */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="0"))
	float TurnResponse = 0.6f;

	/** Max vertical speed in cm/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="0"))
	float MaxDiveSpeed = 150.f;

	/** How quickly vertical speed catches up to the dive input */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="0"))
	float DiveResponse = 0.5f;

	/** Impacts slower than this (cm/s into the surface) don't fire OnHullImpact */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Movement", meta=(ClampMin="0"))
	float MinImpactSpeed = 50.f;

	/** Seconds a client takes to blend out the difference to the server */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Network", meta=(ClampMin="0.01"))
	float CorrectionTime = 0.3f;

	/** Client errors larger than this (cm) snap instead of blending */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Network", meta=(ClampMin="0"))
	float SnapDistance = 300.f;

	/** Inside dimensions (X = length, Y = width, Z = height) of the greybox interior, in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Interior")
	FVector InteriorSize = FVector(2000.f, 600.f, 400.f);

	/** Thickness of the greybox floor/walls, in cm */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Interior", meta=(ClampMin="1"))
	float WallThickness = 20.f;

	/** Doorway size in the walls between helm, sonar and engine room */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Interior")
	FVector2D DoorSize = FVector2D(160.f, 240.f);

	/** Turn off once you have a real hull/interior mesh */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Interior")
	bool bUsePlaceholderInterior = true;

	/** Helm console the server spawns at HelmStationPoint. Clear it to place your own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Stations")
	TSubclassOf<ASubStation> HelmStationClass;

	/** Sonar console the server spawns at SonarStationPoint. Clear it to place your own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Stations")
	TSubclassOf<ASubStation> SonarStationClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Stations")
	TSubclassOf<ASubStation> ElectricStationClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Submarine|Stations")
	TSubclassOf<ASubStation> EngineStationClass;

	/** Server drives the sub through TestManeuvers on a loop, so players can test walking around while it moves */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Test")
	bool bTestManeuvers = false;

	/** Steps the test autopilot loops through */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Test", meta=(TitleProperty="Label"))
	TArray<FSubmarineTestStep> TestManeuvers;

	/**
	 * Actors that always show up on sonar (as amber blips) while within range, even if they're above/below
	 * the ping or small enough to slip between rays. Pick any actors in the level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Test")
	TArray<AActor*> SonarTestTargets;

	/** Print the current test step and speed on screen, separately for server and client */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Test")
	bool bShowTestManeuverDebug = true;

	/** Hitting something faster than this (cm/s into the surface) springs a leak */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Damage", meta=(ClampMin="0"))
	float LeakImpactSpeed = 200.f;

	/** Minimum seconds between new leaks, so one crash doesn't spawn a pile of them */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Damage", meta=(ClampMin="0"))
	float LeakCooldown = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Damage", meta=(ClampMin="1"))
	int32 MaxLeaks = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Damage")
	TSubclassOf<ALeak> LeakClass;

	/** Water depth (cm) at which the sub counts as flooded */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Water", meta=(ClampMin="1"))
	float MaxWaterLevel = 250.f;

	/** Treat the height the sub starts at as the sea surface */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Depth")
	bool bSurfaceAtStart = true;

	/** World Z of the sea surface (used when bSurfaceAtStart is off) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Submarine|Depth", meta=(EditCondition="!bSurfaceAtStart"))
	float SurfaceZ = 0.f;

	/** How fast water drains (cm/s) while there are no leaks */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Water", meta=(ClampMin="0"))
	float DrainRate = 5.f;

	/** Room lights normally: dim and warm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice")
	FLinearColor NormalLightColor = FLinearColor(1.f, 0.8f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice", meta=(ClampMin="0"))
	float NormalLightIntensity = 1500.f;

	/** Peak of the red pulse while there are leaks */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice", meta=(ClampMin="0"))
	float AlarmLightIntensity = 4000.f;

	/** Red pulses per second */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice", meta=(ClampMin="0.1"))
	float AlarmPulseRate = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice", meta=(ClampMin="0", ClampMax="2"))
	float SirenVolume = 0.3f;

	/** Thud played on hull impacts */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice")
	USoundBase* ImpactSound;

	/** Groans played at random while deep; one is picked each time */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice")
	TArray<USoundBase*> CreakSounds;

	/** Depth (cm) where the hull starts creaking */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice", meta=(ClampMin="0"))
	float CreakStartDepth = 2000.f;

	/** Depth (cm) where creaks are at full volume */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice", meta=(ClampMin="0"))
	float CreakFullDepth = 20000.f;

	/** Random seconds between creaks */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Submarine|Juice")
	FVector2D CreakInterval = FVector2D(6.f, 15.f);

private:
	UStaticMeshComponent* CreateInteriorPiece(const FName& Name);
	UPointLightComponent* CreateRoomLight(const FName& Name);
	void LayoutInterior();

	/** Eases speed / turn rate / dive speed toward the current inputs */
	void UpdateRates(float DeltaSeconds, float& Speed, float& TurnRate, float& DiveSpeed) const;

	void UpdateCameraFeed();

	/** Moves the hull one step, sliding along and slowing down on anything it hits */
	void MoveHull(float DeltaSeconds);

	/** Client: blends a little of the remaining error to the server out each frame */
	void ApplyCorrection(float DeltaSeconds);

	/**
	 * Jump the sub somewhere and bring the characters inside along, keeping their place in the sub.
	 * Needed because CharacterMovement follows a moving floor with a swept move, which gets stuck on
	 * anything in between when the floor jumps far. Server moves everyone; clients their own character.
	 */
	void TeleportWithRiders(const FVector& NewLocation, const FRotator& NewRotation);

	/** Is WorldLocation inside the hull box? */
	bool IsInsideHull(const FVector& WorldLocation) const;

	/** Cosmetics on every machine with a screen: alarm lights and siren, creaks at depth */
	void UpdateJuice(float DeltaSeconds);

	/** Thud + camera shake for everyone inside; Strength 0..1 */
	void PlayImpactFeedback(float Strength, const FVector& Location);

	/** Shake the camera of every local player standing inside the sub */
	void ShakeLocalRiders(float Strength) const;

	/** Builds the looping siren sound (generated in code, no asset needed) */
	void BuildSiren();

	/** Server -> everyone: the hull hit something at ImpactSpeed (cm/s into the surface) */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHullImpact(float ImpactSpeed, FVector_NetQuantize Location);

	UPROPERTY(Transient)
	USoundWaveProcedural* SirenWave;

	/** One loop of siren audio (16-bit mono PCM), shared with the audio thread */
	TSharedPtr<TArray<uint8>> SirenPcm;

	bool bAlarmOn = false;
	float AlarmTime = 0.f;
	double NextCreakTime = 0.0;

	/** Server: advances the test autopilot and feeds its inputs */
	void UpdateTestManeuvers(float DeltaSeconds);

	void ShowTestManeuverDebug() const;

	UFUNCTION()
	void OnRep_MoveState();

	UPROPERTY(ReplicatedUsing=OnRep_MoveState)
	FSubmarineMoveState MoveState;

	float Throttle = 0.f;
	float Rudder = 0.f;
	float Dive = 0.f;

	float CurrentSpeed = 0.f;
	float CurrentTurnRate = 0.f;
	float CurrentDiveSpeed = 0.f;

	/** Current test autopilot step, replicated so clients can show it too */
	UPROPERTY(Replicated)
	int32 TestStepIndex = INDEX_NONE;

	float TestStepTimeLeft = 0.f;

	/** Server: true while someone is at the helm */
	bool bAutopilotSuspended = false;

	/** Server: round over, steering is ignored */
	bool bControlsLocked = false;

	/** Server: where the sub started, for resetting rounds */
	FTransform StartTransform;

	/** Server: spawns a station of StationClass at Point and attaches it there */
	void SpawnStation(TSubclassOf<ASubStation> StationClass, USceneComponent* Point);

	/** Server: the stations we spawned */
	UPROPERTY(Transient)
	TArray<ASubStation*> SpawnedStations;

	/** Server: spawn a leak on the inside wall closest to where the hull was hit */
	void SpawnLeakFromImpact(const FHitResult& Hit);

	/** Server: raise the water by every leak's flow, or drain it when there are none */
	void UpdateWater(float DeltaSeconds);

	/** Move/show the water plane to match WaterLevel */
	void UpdateWaterSurface();

	UFUNCTION()
	void OnRep_WaterLevel();

	UPROPERTY(ReplicatedUsing=OnRep_WaterLevel)
	float WaterLevel = 0.f;

	UPROPERTY(Replicated)
	int32 NumLeaks = 0;

	/** Server: open leaks */
	UPROPERTY(Transient)
	TArray<ALeak*> Leaks;

	/** Server: world time of the last leak we spawned */
	double LastLeakTime = -1000.0;

	/** The sub filled with water: game over. Restarting comes with day 12. */
	UPROPERTY(Replicated)
	bool bFlooded = false;

	FVector LocationError = FVector::ZeroVector;
	float YawError = 0.f;
	bool bHasReceivedMoveState = false;

	FTimerHandle CameraCaptureTimer;

	// TEMP debug: how many timer ticks captured vs skipped (screen not visible). Remove after testing.
	int32 DebugCaptureCount = 0;
	int32 DebugSkipCount = 0;
};
