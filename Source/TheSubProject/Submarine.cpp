// Copyright Epic Games, Inc. All Rights Reserved.

#include "Submarine.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace SubmarineInterior
{
	// Order matches the layout in ASubmarine::LayoutInterior
	static const FName PieceNames[] =
	{
		TEXT("Floor"), TEXT("Ceiling"), TEXT("WallLeft"), TEXT("WallRight"), TEXT("WallFront"), TEXT("WallBack"),
		TEXT("HelmDividerLeft"), TEXT("HelmDividerRight"), TEXT("HelmDividerTop"),
		TEXT("EngineDividerLeft"), TEXT("EngineDividerRight"), TEXT("EngineDividerTop"),
	};

	/** Longest a client will extrapolate a snapshot, so a lag spike can't fling the sub */
	static constexpr float MaxExtrapolationTime = 0.5f;
	static constexpr float ExtrapolationStep = 1.f / 60.f;
}

ASubmarine::ASubmarine()
{
	PrimaryActorTick.bCanEverTick = true;
	// Move before characters so anyone standing on the floor follows us in the same frame
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// Server-owned; we replicate our own movement snapshot instead of ReplicatedMovement
	bReplicates = true;
	SetReplicatingMovement(false);
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30.f);
	NetPriority = 3.f;

	// The hull sweeps against level geometry only. Players, projectiles and physics props are
	// ignored so people inside never block it and traces/cameras go straight through.
	HullCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("HullCollision"));
	HullCollision->SetMobility(EComponentMobility::Movable);
	HullCollision->SetCollisionObjectType(ECC_WorldDynamic);
	HullCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HullCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	HullCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	HullCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	HullCollision->SetCanEverAffectNavigation(false);
	RootComponent = HullCollision;

	InteriorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("InteriorRoot"));
	InteriorRoot->SetupAttachment(HullCollision);
	InteriorRoot->SetMobility(EComponentMobility::Movable);

	HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
	HullMesh->SetupAttachment(InteriorRoot);
	HullMesh->SetMobility(EComponentMobility::Movable);

	for (const FName& Name : SubmarineInterior::PieceNames)
	{
		InteriorPieces.Add(CreateInteriorPiece(Name));
	}

	HelmLight = CreateRoomLight(TEXT("HelmLight"));
	SonarLight = CreateRoomLight(TEXT("SonarLight"));
	EngineLight = CreateRoomLight(TEXT("EngineLight"));

	PlayerSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("PlayerSpawnPoint"));
	PlayerSpawnPoint->SetupAttachment(InteriorRoot);
	// Middle (sonar) room, just above the floor so the character capsule (half height ~96) lands on it
	PlayerSpawnPoint->SetRelativeLocation(FVector(0.f, 0.f, 100.f));

	ForwardArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("ForwardArrow"));
	ForwardArrow->SetupAttachment(HullCollision);
	ForwardArrow->ArrowSize = 3.f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		for (UStaticMeshComponent* Piece : InteriorPieces)
		{
			Piece->SetStaticMesh(CubeMesh.Object);
		}
	}

	LayoutInterior();

	// Covers everything the Week 1 test needs: accelerate, turn both ways, stop, reverse, depth changes.
	// Left running it will eventually drive into a wall too.
	TestManeuvers =
	{
		{ TEXT("Full ahead"),  1.f,   0.f,  0.f, 8.f },
		{ TEXT("Hard left"),   0.5f, -1.f,  0.f, 8.f },
		{ TEXT("Hard right"),  0.5f,  1.f,  0.f, 8.f },
		{ TEXT("All stop"),    0.f,   0.f,  0.f, 6.f },
		{ TEXT("Reverse"),    -0.5f,  0.f,  0.f, 6.f },
		{ TEXT("All stop"),    0.f,   0.f,  0.f, 4.f },
		{ TEXT("Dive"),        0.3f,  0.f, -1.f, 4.f },
		{ TEXT("Surface"),     0.3f,  0.f,  1.f, 4.f },
	};
}

UStaticMeshComponent* ASubmarine::CreateInteriorPiece(const FName& Name)
{
	UStaticMeshComponent* Piece = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Piece->SetupAttachment(InteriorRoot);
	Piece->SetMobility(EComponentMobility::Movable);
	Piece->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Piece->SetCanEverAffectNavigation(false);
	return Piece;
}

UPointLightComponent* ASubmarine::CreateRoomLight(const FName& Name)
{
	UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Name);
	Light->SetupAttachment(InteriorRoot);
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetIntensity(5000.f);
	Light->SetAttenuationRadius(900.f);
	Light->SetCastShadows(false);
	return Light;
}

void ASubmarine::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	LayoutInterior();
}

void ASubmarine::LayoutInterior()
{
	// The engine cube is 100cm and centered on its pivot. The floor top sits at the InteriorRoot origin.
	// Rooms along X: helm (front), sonar (middle), engine (back).
	const float L = InteriorSize.X;
	const float W = InteriorSize.Y;
	const float H = InteriorSize.Z;
	const float T = WallThickness;
	const float DoorW = FMath::Clamp(DoorSize.X, 0.f, W);
	const float DoorH = FMath::Clamp(DoorSize.Y, 0.f, H);
	const float SideW = (W - DoorW) * 0.5f;
	const float DividerX = L / 6.f;

	struct FPieceLayout { FVector Center; FVector Size; };
	const FPieceLayout Layout[] =
	{
		{ FVector(0.f, 0.f, -T * 0.5f),             FVector(L + 2.f * T, W + 2.f * T, T) }, // Floor
		{ FVector(0.f, 0.f, H + T * 0.5f),          FVector(L + 2.f * T, W + 2.f * T, T) }, // Ceiling
		{ FVector(0.f, -(W + T) * 0.5f, H * 0.5f),  FVector(L + 2.f * T, T, H) },           // WallLeft
		{ FVector(0.f, (W + T) * 0.5f, H * 0.5f),   FVector(L + 2.f * T, T, H) },           // WallRight
		{ FVector((L + T) * 0.5f, 0.f, H * 0.5f),   FVector(T, W, H) },                     // WallFront
		{ FVector(-(L + T) * 0.5f, 0.f, H * 0.5f),  FVector(T, W, H) },                     // WallBack

		{ FVector(DividerX, -(DoorW + SideW) * 0.5f, H * 0.5f),  FVector(T, SideW, H) },          // HelmDividerLeft
		{ FVector(DividerX, (DoorW + SideW) * 0.5f, H * 0.5f),   FVector(T, SideW, H) },          // HelmDividerRight
		{ FVector(DividerX, 0.f, (DoorH + H) * 0.5f),            FVector(T, DoorW, H - DoorH) },  // HelmDividerTop

		{ FVector(-DividerX, -(DoorW + SideW) * 0.5f, H * 0.5f), FVector(T, SideW, H) },          // EngineDividerLeft
		{ FVector(-DividerX, (DoorW + SideW) * 0.5f, H * 0.5f),  FVector(T, SideW, H) },          // EngineDividerRight
		{ FVector(-DividerX, 0.f, (DoorH + H) * 0.5f),           FVector(T, DoorW, H - DoorH) },  // EngineDividerTop
	};
	static_assert(UE_ARRAY_COUNT(Layout) == UE_ARRAY_COUNT(SubmarineInterior::PieceNames), "Every interior piece needs a layout");

	for (int32 Index = 0; Index < InteriorPieces.Num(); ++Index)
	{
		UStaticMeshComponent* Piece = InteriorPieces[Index];
		Piece->SetRelativeLocation(Layout[Index].Center);
		Piece->SetRelativeScale3D(Layout[Index].Size / 100.f);
		Piece->SetVisibility(bUsePlaceholderInterior);
		Piece->SetCollisionEnabled(bUsePlaceholderInterior ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}

	const float LightZ = H - 60.f;
	HelmLight->SetRelativeLocation(FVector(L / 3.f, 0.f, LightZ));
	SonarLight->SetRelativeLocation(FVector(0.f, 0.f, LightZ));
	EngineLight->SetRelativeLocation(FVector(-L / 3.f, 0.f, LightZ));

	// Hull box wraps the whole interior and is centered on it
	HullCollision->SetBoxExtent(FVector(L * 0.5f + T, W * 0.5f + T, H * 0.5f + T));
	InteriorRoot->SetRelativeLocation(FVector(0.f, 0.f, -H * 0.5f));
}

void ASubmarine::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		Throttle = InitialThrottle;
	}
}

void ASubmarine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASubmarine, MoveState);
	DOREPLIFETIME(ASubmarine, TestStepIndex);
}

void ASubmarine::SetThrottle(float Value)
{
	if (HasAuthority())
	{
		Throttle = FMath::Clamp(Value, -1.f, 1.f);
	}
}

void ASubmarine::SetRudder(float Value)
{
	if (HasAuthority())
	{
		Rudder = FMath::Clamp(Value, -1.f, 1.f);
	}
}

void ASubmarine::SetDive(float Value)
{
	if (HasAuthority())
	{
		Dive = FMath::Clamp(Value, -1.f, 1.f);
	}
}

void ASubmarine::AllStop()
{
	SetThrottle(0.f);
	SetRudder(0.f);
	SetDive(0.f);
}

void ASubmarine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		UpdateTestManeuvers(DeltaSeconds);
	}

	// Server and clients run the same simulation; the server's result is the truth
	UpdateRates(DeltaSeconds, CurrentSpeed, CurrentTurnRate, CurrentDiveSpeed);
	MoveHull(DeltaSeconds);

	if (HasAuthority())
	{
		MoveState.Location = GetActorLocation();
		MoveState.Yaw = GetActorRotation().Yaw;
		MoveState.Speed = CurrentSpeed;
		MoveState.TurnRate = CurrentTurnRate;
		MoveState.DiveSpeed = CurrentDiveSpeed;
		MoveState.Throttle = Throttle;
		MoveState.Rudder = Rudder;
		MoveState.Dive = Dive;
		MoveState.ServerTime = GetServerTime();
	}
	else
	{
		ApplyCorrection(DeltaSeconds);
	}

	if (bShowTestManeuverDebug)
	{
		ShowTestManeuverDebug();
	}
}

void ASubmarine::UpdateTestManeuvers(float DeltaSeconds)
{
	if (!bTestManeuvers || TestManeuvers.Num() == 0)
	{
		// Switched off mid-run: don't leave the sub driving on the last step's inputs
		if (TestStepIndex != INDEX_NONE)
		{
			TestStepIndex = INDEX_NONE;
			AllStop();
		}
		return;
	}

	TestStepTimeLeft -= DeltaSeconds;
	if (TestStepIndex == INDEX_NONE || TestStepTimeLeft <= 0.f)
	{
		TestStepIndex = (TestStepIndex + 1) % TestManeuvers.Num();
		const FSubmarineTestStep& Step = TestManeuvers[TestStepIndex];
		SetThrottle(Step.Throttle);
		SetRudder(Step.Rudder);
		SetDive(Step.Dive);
		TestStepTimeLeft = FMath::Max(Step.Duration, 0.1f);
	}
}

void ASubmarine::ShowTestManeuverDebug() const
{
	if (GEngine == nullptr || !TestManeuvers.IsValidIndex(TestStepIndex))
	{
		return;
	}

	// Separate lines for server and client so you can compare them side by side
	const bool bIsServer = HasAuthority();
	const uint64 MessageKey = GetUniqueID() * 2 + (bIsServer ? 0 : 1);
	const FString Message = FString::Printf(TEXT("[%s] Sub test %d/%d: %s | speed %.0f cm/s, turn %.1f deg/s, dive %.0f cm/s"),
		bIsServer ? TEXT("Server") : TEXT("Client"),
		TestStepIndex + 1, TestManeuvers.Num(), *TestManeuvers[TestStepIndex].Label.ToString(),
		CurrentSpeed, CurrentTurnRate, CurrentDiveSpeed);
	GEngine->AddOnScreenDebugMessage(MessageKey, 0.f, bIsServer ? FColor::Cyan : FColor::Yellow, Message);
}

void ASubmarine::UpdateRates(float DeltaSeconds, float& Speed, float& TurnRate, float& DiveSpeed) const
{
	// Ease every axis toward its target so the hull glides instead of jerking/snaking
	Speed = FMath::FInterpTo(Speed, Throttle * MaxSpeed, DeltaSeconds, Acceleration);
	TurnRate = FMath::FInterpTo(TurnRate, Rudder * MaxTurnRate, DeltaSeconds, TurnResponse);
	DiveSpeed = FMath::FInterpTo(DiveSpeed, Dive * MaxDiveSpeed, DeltaSeconds, DiveResponse);
}

void ASubmarine::MoveHull(float DeltaSeconds)
{
	// Yaw only: keeping the deck level makes walking around inside comfortable.
	// Rotation isn't swept (engine limitation), only the translation below is.
	const FRotator NewRotation = GetActorRotation() + FRotator(0.f, CurrentTurnRate * DeltaSeconds, 0.f);
	SetActorRotation(NewRotation);

	const FVector Forward = FRotator(0.f, NewRotation.Yaw, 0.f).Vector();
	const FVector Velocity = Forward * CurrentSpeed + FVector::UpVector * CurrentDiveSpeed;
	const FVector Delta = Velocity * DeltaSeconds;
	if (Delta.IsNearlyZero())
	{
		return;
	}

	FHitResult Hit;
	AddActorWorldOffset(Delta, true, &Hit);
	if (!Hit.IsValidBlockingHit())
	{
		return;
	}

	// Kill the part of our velocity going into the surface, keep the rest
	const float IntoSurface = -(Velocity | Hit.Normal);
	if (IntoSurface > 0.f)
	{
		const FVector NewVelocity = Velocity + Hit.Normal * IntoSurface;
		CurrentSpeed = NewVelocity | Forward;
		CurrentDiveSpeed = NewVelocity.Z;

		if (HasAuthority() && IntoSurface >= MinImpactSpeed)
		{
			OnHullImpact.Broadcast(IntoSurface, Hit);
		}
	}

	// Slide along whatever we hit with the rest of this frame's movement
	const FVector Slide = FVector::VectorPlaneProject(Delta * (1.f - Hit.Time), Hit.Normal);
	if (!Slide.IsNearlyZero())
	{
		AddActorWorldOffset(Slide, true);
	}
}

void ASubmarine::OnRep_MoveState()
{
	Throttle = MoveState.Throttle;
	Rudder = MoveState.Rudder;
	Dive = MoveState.Dive;

	// The snapshot is from the past: run it forward by the lag to guess where the server is now
	float Speed = MoveState.Speed;
	float TurnRate = MoveState.TurnRate;
	float DiveSpeed = MoveState.DiveSpeed;
	FVector Location = MoveState.Location;
	float Yaw = MoveState.Yaw;

	float Remaining = FMath::Clamp(static_cast<float>(GetServerTime() - MoveState.ServerTime), 0.f, SubmarineInterior::MaxExtrapolationTime);
	while (Remaining > UE_KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Remaining, SubmarineInterior::ExtrapolationStep);
		UpdateRates(Step, Speed, TurnRate, DiveSpeed);
		Yaw += TurnRate * Step;
		Location += (FRotator(0.f, Yaw, 0.f).Vector() * Speed + FVector::UpVector * DiveSpeed) * Step;
		Remaining -= Step;
	}

	CurrentSpeed = Speed;
	CurrentTurnRate = TurnRate;
	CurrentDiveSpeed = DiveSpeed;

	const FRotator CurrentRotation = GetActorRotation();
	const FVector Error = Location - GetActorLocation();
	if (!bHasReceivedMoveState || Error.SizeSquared() > FMath::Square(SnapDistance))
	{
		SetActorLocationAndRotation(Location, FRotator(CurrentRotation.Pitch, Yaw, CurrentRotation.Roll), false, nullptr, ETeleportType::TeleportPhysics);
		LocationError = FVector::ZeroVector;
		YawError = 0.f;
		bHasReceivedMoveState = true;
	}
	else
	{
		LocationError = Error;
		YawError = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, Yaw);
	}
}

void ASubmarine::ApplyCorrection(float DeltaSeconds)
{
	if (LocationError.IsNearlyZero() && FMath::IsNearlyZero(YawError))
	{
		return;
	}

	const float Alpha = 1.f - FMath::Exp(-DeltaSeconds / CorrectionTime);
	const FVector LocationFix = LocationError * Alpha;
	const float YawFix = YawError * Alpha;
	LocationError -= LocationFix;
	YawError -= YawFix;

	SetActorLocationAndRotation(GetActorLocation() + LocationFix, GetActorRotation() + FRotator(0.f, YawFix, 0.f));
}

double ASubmarine::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

FTransform ASubmarine::GetPlayerSpawnTransform() const
{
	return PlayerSpawnPoint->GetComponentTransform();
}
