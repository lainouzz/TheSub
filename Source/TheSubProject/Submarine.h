// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Submarine.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UArrowComponent;

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

	/** Greybox floor, ceiling, outer walls and room dividers, sized by InteriorSize */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	TArray<UStaticMeshComponent*> InteriorPieces;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UPointLightComponent* HelmLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UPointLightComponent* SonarLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UPointLightComponent* EngineLight;

	/** Where players spawn inside the sub (move it in the Blueprint/viewport) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	USceneComponent* PlayerSpawnPoint;

	/** Shows which way is forward in the editor */
	UPROPERTY(VisibleAnywhere, Category=Submarine, meta=(AllowPrivateAccess = "true"))
	UArrowComponent* ForwardArrow;

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

	/** World transform the player should spawn at */
	UFUNCTION(BlueprintPure, Category="Submarine")
	FTransform GetPlayerSpawnTransform() const;

	/** Fires on the server when the hull hits something hard enough (hook hull damage in here later) */
	UPROPERTY(BlueprintAssignable, Category="Submarine")
	FOnSubmarineImpact OnHullImpact;

protected:
	virtual void BeginPlay() override;

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

	/** Server drives the sub through TestManeuvers on a loop, so players can test walking around while it moves */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Test")
	bool bTestManeuvers = false;

	/** Steps the test autopilot loops through */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Test", meta=(TitleProperty="Label"))
	TArray<FSubmarineTestStep> TestManeuvers;

	/** Print the current test step and speed on screen, separately for server and client */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Submarine|Test")
	bool bShowTestManeuverDebug = true;

private:
	UStaticMeshComponent* CreateInteriorPiece(const FName& Name);
	UPointLightComponent* CreateRoomLight(const FName& Name);
	void LayoutInterior();

	/** Eases speed / turn rate / dive speed toward the current inputs */
	void UpdateRates(float DeltaSeconds, float& Speed, float& TurnRate, float& DiveSpeed) const;

	/** Moves the hull one step, sliding along and slowing down on anything it hits */
	void MoveHull(float DeltaSeconds);

	/** Client: blends a little of the remaining error to the server out each frame */
	void ApplyCorrection(float DeltaSeconds);

	double GetServerTime() const;

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

	FVector LocationError = FVector::ZeroVector;
	float YawError = 0.f;
	bool bHasReceivedMoveState = false;
};
