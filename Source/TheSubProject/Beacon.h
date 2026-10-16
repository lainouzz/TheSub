// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Beacon.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * The objective at the bottom of the trench. The server marks it reached once a submarine gets within
 * ReachRadius. It always shows up on sonar so the sonar operator can guide the pilot to it.
 */
UCLASS()
class THESUBPROJECT_API ABeacon : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Beacon, meta=(AllowPrivateAccess = "true"))
	UStaticMeshComponent* Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Beacon, meta=(AllowPrivateAccess = "true"))
	UPointLightComponent* Light;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Beacon, meta=(AllowPrivateAccess = "true"))
	UTextRenderComponent* Label;

public:
	ABeacon();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: new round, not reached yet */
	virtual void Reset() override;

	UFUNCTION(BlueprintPure, Category="Beacon")
	bool IsReached() const { return bReached; }

	/** Server, debug: count the beacon as reached */
	void DebugMarkReached();

protected:
	/** How close (cm) the submarine has to get */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Beacon", meta=(ClampMin="0"))
	float ReachRadius = 2000.f;

private:
	UFUNCTION()
	void OnRep_Reached();

	/** Light and label switch to "reached" */
	void UpdateLook();

	UPROPERTY(ReplicatedUsing=OnRep_Reached)
	bool bReached = false;
};
