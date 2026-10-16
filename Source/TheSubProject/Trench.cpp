// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trench.h"
#include "Beacon.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace TrenchDefaults
{
	/** Distance from the end of the path to the beacon, so it isn't jammed into the end cap */
	static constexpr float BeaconInset = 1000.f;

	/** Half the beacon pillar's height (engine cylinder is 100cm tall, the beacon scales it by 3) */
	static constexpr float BeaconHalfHeight = 150.f;
}

ATrench::ATrench()
{
	PrimaryActorTick.bCanEverTick = false;

	Path = CreateDefaultSubobject<USplineComponent>(TEXT("Path"));
	RootComponent = Path;

	// Roughly 750m long and 220m down with a few S-turns: about 5 minutes at half ahead
	Path->SetSplinePoints(
		{
			FVector(0.f, 0.f, 0.f),
			FVector(15000.f, 0.f, -2000.f),
			FVector(25000.f, 8000.f, -6000.f),
			FVector(35000.f, 8000.f, -9000.f),
			FVector(45000.f, -2000.f, -13000.f),
			FVector(55000.f, -2000.f, -16000.f),
			FVector(65000.f, 6000.f, -20000.f),
			FVector(75000.f, 6000.f, -22000.f),
		},
		ESplineCoordinateSpace::Local);

	Rock = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Rock"));
	Rock->SetupAttachment(Path);
	Rock->SetMobility(EComponentMobility::Movable);
	Rock->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Rock->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Rock->SetStaticMesh(CubeMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GridMaterial(TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	if (GridMaterial.Succeeded())
	{
		Rock->SetMaterial(0, GridMaterial.Object);
	}

	BeaconClass = ABeacon::StaticClass();
}

void ATrench::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	BuildRock();
}

void ATrench::BuildRock()
{
	Rock->ClearInstances();

	const float Length = Path->GetSplineLength();
	const float T = WallThickness;
	const float WallCenterZ = WallHeight * 0.5f - FloorDepth;

	// The engine cube is 100cm, centered on its pivot
	auto MakeBlock = [](const FRotator& Rotation, const FVector& Center, const FVector& Offset, const FVector& Size)
	{
		return FTransform(Rotation, Center + Rotation.RotateVector(Offset), Size / 100.f);
	};

	TArray<FTransform> Blocks;
	for (float Start = 0.f; Start < Length; Start += SegmentLength)
	{
		const float End = FMath::Min(Start + SegmentLength, Length);
		const float Middle = (Start + End) * 0.5f;

		// Follow the path's heading and slope, but never roll, so walls stay upright
		const FVector Center = Path->GetLocationAtDistanceAlongSpline(Middle, ESplineCoordinateSpace::Local);
		const FRotator PathRotation = Path->GetRotationAtDistanceAlongSpline(Middle, ESplineCoordinateSpace::Local);
		const FRotator Rotation(PathRotation.Pitch, PathRotation.Yaw, 0.f);

		// Longer than the segment so neighbours overlap and turns don't leave gaps
		const float BlockLength = (End - Start) + T * 2.f;

		Blocks.Add(MakeBlock(Rotation, Center, FVector(0.f, 0.f, -FloorDepth - T * 0.5f), FVector(BlockLength, Width + T * 2.f, T)));
		Blocks.Add(MakeBlock(Rotation, Center, FVector(0.f, -(Width + T) * 0.5f, WallCenterZ), FVector(BlockLength, T, WallHeight)));
		Blocks.Add(MakeBlock(Rotation, Center, FVector(0.f, (Width + T) * 0.5f, WallCenterZ), FVector(BlockLength, T, WallHeight)));
	}

	if (bCapEnd && Length > 0.f)
	{
		const FVector EndPoint = Path->GetLocationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::Local);
		const FRotator EndRotation(0.f, Path->GetRotationAtDistanceAlongSpline(Length, ESplineCoordinateSpace::Local).Yaw, 0.f);
		Blocks.Add(MakeBlock(EndRotation, EndPoint, FVector(T * 0.5f, 0.f, WallCenterZ), FVector(T, Width + T * 2.f, WallHeight)));
	}

	Rock->AddInstances(Blocks, false, false, false);
}

void ATrench::BeginPlay()
{
	Super::BeginPlay();

	// The beacon is replicated, so only the server spawns it
	if (!HasAuthority() || BeaconClass == nullptr)
	{
		return;
	}

	const float Distance = FMath::Max(0.f, Path->GetSplineLength() - TrenchDefaults::BeaconInset);
	const FVector PathPoint = Path->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	const FRotator PathRotation = Path->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	const FRotator Upright(PathRotation.Pitch, PathRotation.Yaw, 0.f);

	// Standing on the floor, which is FloorDepth below the path
	const FVector BeaconLocation = PathPoint + Upright.RotateVector(FVector(0.f, 0.f, -FloorDepth + TrenchDefaults::BeaconHalfHeight));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ABeacon>(BeaconClass, FTransform(FRotator(0.f, PathRotation.Yaw + 180.f, 0.f), BeaconLocation), SpawnParams);
}
