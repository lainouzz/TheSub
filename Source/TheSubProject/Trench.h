// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Trench.generated.h"

class ABeacon;
class UInstancedStaticMeshComponent;
class USplineComponent;

/**
 * A hand-made trench: drag the spline points around in the viewport and the canyon (floor + two walls)
 * is rebuilt along it. The spline marks the path the sub should follow, at the sub's height;
 * the floor sits FloorDepth below it. The server spawns the objective beacon at the far end.
 */
UCLASS()
class THESUBPROJECT_API ATrench : public AActor
{
	GENERATED_BODY()

	/** The route down. Alt-drag a point to add one. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Trench, meta=(AllowPrivateAccess = "true"))
	USplineComponent* Path;

	/** All floor and wall blocks, as instances of one cube */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Trench, meta=(AllowPrivateAccess = "true"))
	UInstancedStaticMeshComponent* Rock;

public:
	ATrench();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

	/** Distance between the walls, in cm (the sub is about 2000 long and 640 wide) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trench", meta=(ClampMin="500"))
	float Width = 3000.f;

	/** How far the floor is below the path, in cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trench", meta=(ClampMin="0"))
	float FloorDepth = 800.f;

	/** How high the walls reach above the floor, in cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trench", meta=(ClampMin="100"))
	float WallHeight = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trench", meta=(ClampMin="10"))
	float WallThickness = 200.f;

	/** Length of each wall/floor block along the path; shorter = smoother turns, more blocks */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trench", meta=(ClampMin="100"))
	float SegmentLength = 1000.f;

	/** Close off the far end behind the beacon */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trench")
	bool bCapEnd = true;

	/** Objective the server spawns on the floor at the end of the path; clear it to place your own */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trench")
	TSubclassOf<ABeacon> BeaconClass;

private:
	/** Rebuilds all floor and wall blocks along the path */
	void BuildRock();
};
