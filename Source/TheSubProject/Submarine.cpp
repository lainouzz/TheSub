// Copyright Epic Games, Inc. All Rights Reserved.

#include "Submarine.h"
#include "HelmStation.h"
#include "SonarStation.h"
#include "EngineStation.h"
#include "ElectricStation.h"
#include "Leak.h"
#include "HullShakeCameraModifier.h"
#include "Materials/MaterialInterface.h"
#include "Components/AudioComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Sound/SoundWaveProcedural.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogSubmarine);

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

	/** Extra distance (cm) to push the hull out when it starts a move overlapping something */
	static constexpr float DepenetrationPadding = 0.5f;

	/** Deepest overlap (cm) the hull will push itself out of in one go */
	static constexpr float MaxDepenetration = 30.f;
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

	NoseSpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("NoseLight"));
	NoseSpotLight->SetupAttachment(InteriorRoot);

	HallRoofwayLight_1 = CreateRoomLight(TEXT("RoofLight 1"));
	HallRoofwayLight_2 = CreateRoomLight(TEXT("RoofLight 2"));

	HallwayRedLight_1 = CreateRoomLight(TEXT("RedLight 1"));
	HallwayRedLight_2 = CreateRoomLight(TEXT("RedLight 2"));
	HallwayRedLight_3 = CreateRoomLight(TEXT("RedLight 3"));
	HallwayRedLight_4 = CreateRoomLight(TEXT("RedLight 4"));

	// Plays inside the sub, so no 3D attenuation: everyone aboard hears it the same
	AlarmAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("AlarmAudio"));
	AlarmAudio->SetupAttachment(InteriorRoot);
	AlarmAudio->bAutoActivate = false;
	AlarmAudio->bAllowSpatialization = false;

	HelmScreenComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("HelmScreenComponent"));
	HelmScreenComponent->SetupAttachment(InteriorRoot);

	HelmScreenComponent->bCaptureEveryFrame = false;
	HelmScreenComponent->bCaptureOnMovement = false;

	// Starter Content sounds, pitched down at play time: a debris thud for impacts, collapses as hull groans
	static ConstructorHelpers::FObjectFinder<USoundBase> ImpactSoundAsset(TEXT("/Game/StarterContent/Audio/Explosion01.Explosion01"));
	if (ImpactSoundAsset.Succeeded())
	{
		ImpactSound = ImpactSoundAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<USoundBase> Creak1(TEXT("/Game/StarterContent/Audio/Collapse01.Collapse01"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Creak2(TEXT("/Game/StarterContent/Audio/Collapse02.Collapse02"));
	for (USoundBase* Creak : { Creak1.Object, Creak2.Object })
	{
		if (Creak != nullptr)
		{
			CreakSounds.Add(Creak);
		}
	}

	PlayerSpawnPoint = CreateDefaultSubobject<USceneComponent>(TEXT("PlayerSpawnPoint"));
	PlayerSpawnPoint->SetupAttachment(InteriorRoot);
	// Middle (sonar) room, just above the floor so the character capsule (half height ~96) lands on it
	PlayerSpawnPoint->SetRelativeLocation(FVector(0.f, 0.f, 100.f));

	HelmStationPoint = CreateDefaultSubobject<USceneComponent>(TEXT("HelmStationPoint"));
	HelmStationPoint->SetupAttachment(InteriorRoot);
	HelmStationClass = AHelmStation::StaticClass();

	SonarStationPoint = CreateDefaultSubobject<USceneComponent>(TEXT("SonarStationPoint"));
	SonarStationPoint->SetupAttachment(InteriorRoot);
	SonarStationClass = ASonarStation::StaticClass();

	ElectricStationPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ElectricSpawnPoint"));
	ElectricStationPoint->SetupAttachment(InteriorRoot);
	ElectricStationClass = AElectricStation::StaticClass();

	EngineStationPoint = CreateDefaultSubobject<USceneComponent>(TEXT("EngineSpawnPoint"));
	EngineStationPoint->SetupAttachment(InteriorRoot);
	EngineStationClass = AEngineStation::StaticClass();


	LeakClass = ALeak::StaticClass();

	// Engine plane: 100x100cm, facing up, scaled to the interior in LayoutInterior
	WaterSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterSurface"));
	WaterSurface->SetupAttachment(InteriorRoot);
	WaterSurface->SetMobility(EComponentMobility::Movable);
	WaterSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterSurface->SetCanEverAffectNavigation(false);
	WaterSurface->SetCastShadow(false);
	WaterSurface->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		WaterSurface->SetStaticMesh(PlaneMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WaterMaterial(TEXT("/Game/StarterContent/Materials/M_Water_Lake.M_Water_Lake"));
	if (WaterMaterial.Succeeded())
	{
		WaterSurface->SetMaterial(0, WaterMaterial.Object);
	}

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
	Light->SetIntensity(NormalLightIntensity);
	Light->SetLightColor(NormalLightColor);
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

	if (bUsePlaceholderInterior) {
		const float LightZ = H - 60.f;
		//Lights
		HelmLight->SetRelativeLocation(FVector(L / 3.f, 0.f, LightZ));
		SonarLight->SetRelativeLocation(FVector(0.f, 0.f, LightZ));
		EngineLight->SetRelativeLocation(FVector(-L / 3.f, 0.f, LightZ));

		// Console a little off the bow wall, facing forward, so the pilot stands behind it
		HelmStationPoint->SetRelativeLocation(FVector(L * 0.5f - 100.f, 0.f, 0.f));

		// Console against the port (left) wall, facing it, so the operator stands in the room with their back to the middle
		SonarStationPoint->SetRelativeLocation(FVector(0.f, -W * 0.5f + 100.f, 0.f));
		SonarStationPoint->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	}

	// Hull box wraps the whole interior and is centered on it
	HullCollision->SetBoxExtent(FVector(L * 0.5f + T, W * 0.5f + T, H * 0.5f + T));
	InteriorRoot->SetRelativeLocation(FVector(0.f, 0.f, -H * 0.5f));

	WaterSurface->SetRelativeScale3D(FVector(L / 100.f, W / 100.f, 1.f));
	UpdateWaterSurface();
}

void ASubmarine::BeginPlay()
{
	Super::BeginPlay();

	// Sound and light effects only matter where someone is looking at a screen
	if (GetNetMode() != NM_DedicatedServer)
	{
		BuildSiren();
		NextCreakTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(CreakInterval.X, CreakInterval.Y);

		if (HelmScreenComponent) {
			UTextureRenderTarget2D* CameraRenderTarget =
				UKismetRenderingLibrary::CreateRenderTarget2D(
					this,
					320,
					180,
					ETextureRenderTargetFormat::RTF_RGBA8,
					FLinearColor::Black,
					false,
					false
				);

			HelmScreenComponent->HiddenActors.Add(this);

			if (CameraRenderTarget)
			{
				HelmScreenComponent->TextureTarget = CameraRenderTarget;
			}

			TArray<UPrimitiveComponent*> PrimitiveComponents;
			GetComponents<UPrimitiveComponent>(PrimitiveComponents);

			for (UPrimitiveComponent* Components : PrimitiveComponents)
			{
				if (Components && Components->ComponentHasTag(FName("HelmScreen")))
				{
					HelmScreenMesh = Components;
					break;
				}
			}

			if (HelmScreenMesh)
			{
				UMaterialInstanceDynamic* ScreenMID = HelmScreenMesh->CreateDynamicMaterialInstance(0);

				if (ScreenMID && CameraRenderTarget)
				{
					ScreenMID->SetTextureParameterValue(
						FName("CameraFeed"),
						CameraRenderTarget
					);
				}
			}

			GetWorldTimerManager().SetTimer(
				CameraCaptureTimer,
				this,
				&ASubmarine::UpdateCameraFeed,
				1.f / 12.f,
				true
			);
			
		}
	}

	if (HasAuthority())
	{
		Throttle = InitialThrottle;

		if (bSurfaceAtStart)
		{
			SurfaceZ = GetActorLocation().Z;
		}
		StartTransform = GetActorTransform();

		// Stations are replicated actors attached to the sub; clients get them through replication
		SpawnStation(HelmStationClass, HelmStationPoint);
		SpawnStation(SonarStationClass, SonarStationPoint);
		SpawnStation(EngineStationClass, EngineStationPoint);
		SpawnStation(ElectricStationClass, ElectricStationPoint);
	}
}

void ASubmarine::SpawnStation(TSubclassOf<ASubStation> StationClass, USceneComponent* Point)
{
	if (StationClass == nullptr)
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ASubStation* Station = GetWorld()->SpawnActor<ASubStation>(StationClass, Point->GetComponentTransform(), SpawnParams))
	{
		Station->AttachToComponent(Point, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		SpawnedStations.Add(Station);
	}
}

void ASubmarine::SpawnLeakFromImpact(const FHitResult& Hit)
{
	Leaks.RemoveAll([](const ALeak* Leak) { return !IsValid(Leak); });

	const double Now = GetWorld()->GetTimeSeconds();
	if (LeakClass == nullptr || Leaks.Num() >= MaxLeaks || Now - LastLeakTime < LeakCooldown)
	{
		return;
	}

	// Work in interior space: floor top at Z = 0, rooms centered on X/Y = 0
	const float HalfL = InteriorSize.X * 0.5f;
	const float HalfW = InteriorSize.Y * 0.5f;
	const float H = InteriorSize.Z;
	const float EdgeMargin = 50.f;
	const FVector Impact = InteriorRoot->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);

	// Put the leak on the inside of whichever wall is closest to the impact, facing into the room (+X)
	FVector LeakLocation;
	float LeakYaw;
	if (FMath::Abs(Impact.X) / HalfL >= FMath::Abs(Impact.Y) / HalfW)
	{
		const float Side = Impact.X >= 0.f ? 1.f : -1.f;
		LeakLocation = FVector(Side * HalfL, FMath::Clamp(Impact.Y, -HalfW + EdgeMargin, HalfW - EdgeMargin), 0.f);
		LeakYaw = Side > 0.f ? 180.f : 0.f;
	}
	else
	{
		const float Side = Impact.Y >= 0.f ? 1.f : -1.f;
		LeakLocation = FVector(FMath::Clamp(Impact.X, -HalfL + EdgeMargin, HalfL - EdgeMargin), Side * HalfW, 0.f);
		LeakYaw = Side > 0.f ? -90.f : 90.f;
	}
	// Anywhere from knee to just under the ceiling, so some are easy to reach and some aren't
	LeakLocation.Z = FMath::FRandRange(60.f, H - 80.f);

	const FTransform LeakTransform = FTransform(FRotator(0.f, LeakYaw, 0.f), LeakLocation) * InteriorRoot->GetComponentTransform();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ALeak* Leak = GetWorld()->SpawnActor<ALeak>(LeakClass, LeakTransform, SpawnParams))
	{
		Leak->AttachToComponent(InteriorRoot, FAttachmentTransformRules::KeepWorldTransform);
		Leaks.Add(Leak);
		NumLeaks = Leaks.Num();
		LastLeakTime = Now;

		UE_LOG(LogSubmarine, Log, TEXT("Hull breach! Leak %d/%d at %s"), Leaks.Num(), MaxLeaks, *LeakLocation.ToString());
	}
}

void ASubmarine::UpdateWater(float DeltaSeconds)
{
	Leaks.RemoveAll([](const ALeak* Leak) { return !IsValid(Leak); });
	NumLeaks = Leaks.Num();

	// Game over: the sub stays full
	if (bFlooded)
	{
		return;
	}

	float FlowRate = 0.f;
	for (const ALeak* Leak : Leaks)
	{
		FlowRate += Leak->GetFlowRate();
	}

	const float OldLevel = WaterLevel;
	WaterLevel = FMath::Clamp(WaterLevel + (Leaks.Num() > 0 ? FlowRate : -DrainRate) * DeltaSeconds, 0.f, MaxWaterLevel);
	if (WaterLevel != OldLevel)
	{
		UpdateWaterSurface();
	}

	// Full of water = lost (the game mode picks this up and ends the round)
	if (WaterLevel >= MaxWaterLevel && !bFlooded)
	{
		AllStop();
		bFlooded = true;
		UE_LOG(LogSubmarine, Warning, TEXT("Submarine flooded! Game over."));
	}
}

void ASubmarine::OnRep_WaterLevel()
{
	UpdateWaterSurface();
}

void ASubmarine::UpdateWaterSurface()
{
	WaterSurface->SetRelativeLocation(FVector(0.f, 0.f, WaterLevel));
	WaterSurface->SetVisibility(WaterLevel > 1.f);
}

void ASubmarine::SetControlsLocked(bool bLocked)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bLocked)
	{
		AllStop();
	}
	bControlsLocked = bLocked;
}

void ASubmarine::Reset()
{
	Super::Reset();

	if (!HasAuthority())
	{
		return;
	}

	// Leaks remove themselves on Reset too; this just makes sure none are left behind
	for (ALeak* Leak : Leaks)
	{
		if (IsValid(Leak))
		{
			Leak->Destroy();
		}
	}
	Leaks.Reset();
	NumLeaks = 0;
	LastLeakTime = -1000.0;

	WaterLevel = 0.f;
	bFlooded = false;
	UpdateWaterSurface();

	bControlsLocked = false;
	bAutopilotSuspended = false;
	TestStepIndex = INDEX_NONE;
	TestStepTimeLeft = 0.f;
	Throttle = InitialThrottle;
	Rudder = 0.f;
	Dive = 0.f;
	CurrentSpeed = 0.f;
	CurrentTurnRate = 0.f;
	CurrentDiveSpeed = 0.f;

	// Clients see the jump through the next movement snapshot (big errors snap instead of blending)
	SetActorTransform(StartTransform, false, nullptr, ETeleportType::TeleportPhysics);
}

void ASubmarine::DebugTeleport(const FVector& Location, const FRotator& Rotation)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentSpeed = 0.f;
	CurrentTurnRate = 0.f;
	CurrentDiveSpeed = 0.f;

	// Clients see it through the next movement snapshot: the jump is beyond SnapDistance, so they snap too
	TeleportWithRiders(Location, Rotation);
	UE_LOG(LogSubmarine, Log, TEXT("Debug: teleported to %s (depth %.0f m)"), *Location.ToString(), GetDepth() / 100.f);
}

bool ASubmarine::IsInsideHull(const FVector& WorldLocation) const
{
	const FVector Local = FTransform(GetActorRotation(), GetActorLocation()).InverseTransformPosition(WorldLocation);
	const FVector Extent = HullCollision->GetScaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

void ASubmarine::TeleportWithRiders(const FVector& NewLocation, const FRotator& NewRotation)
{
	// Everyone inside the hull box, standing or mid-jump, with their transform relative to the sub
	const FTransform OldSubTransform(GetActorRotation(), GetActorLocation());
	TArray<TPair<ACharacter*, FTransform>> Riders;
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		ACharacter* Character = *It;
		// Clients only move their own character; the server corrects everyone else
		if (!HasAuthority() && !Character->IsLocallyControlled())
		{
			continue;
		}

		if (IsInsideHull(Character->GetActorLocation()))
		{
			Riders.Emplace(Character, Character->GetActorTransform().GetRelativeTransform(OldSubTransform));
		}
	}

	SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::TeleportPhysics);

	const FTransform NewSubTransform(NewRotation, NewLocation);
	for (const TPair<ACharacter*, FTransform>& Rider : Riders)
	{
		const FTransform RiderTransform = Rider.Value * NewSubTransform;
		Rider.Key->SetActorLocationAndRotation(RiderTransform.GetLocation(), RiderTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);

		// The floor's new position is the new baseline, or the next based-movement update would move them a second time
		Rider.Key->GetCharacterMovement()->SaveBaseLocation();
	}
}

void ASubmarine::DebugFlood()
{
	if (!HasAuthority())
	{
		return;
	}

	// Set it straight away: with no leaks, UpdateWater would drain a bit before checking for a flood
	WaterLevel = MaxWaterLevel;
	UpdateWaterSurface();
	AllStop();
	bFlooded = true;
	UE_LOG(LogSubmarine, Log, TEXT("Debug: flooded"));
}

void ASubmarine::DebugSpawnLeak()
{
	if (!HasAuthority())
	{
		return;
	}

	// Pretend something hit us from a random direction; SpawnLeakFromImpact picks the wall facing it
	FHitResult FakeHit;
	FakeHit.ImpactPoint = GetActorLocation() + FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).RotateVector(GetActorForwardVector()) * 5000.f;
	LastLeakTime = -1000.0;
	SpawnLeakFromImpact(FakeHit);
}

float ASubmarine::GetHullHalfHeight() const
{
	return HullCollision->GetScaledBoxExtent().Z;
}

float ASubmarine::GetWaterSurfaceZ() const
{
	return WaterSurface->GetComponentLocation().Z;
}

void ASubmarine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		for (ASubStation* Station : SpawnedStations)
		{
			if (Station != nullptr)
			{
				Station->Destroy();
			}
		}
		SpawnedStations.Reset();

		for (ALeak* Leak : Leaks)
		{
			if (IsValid(Leak))
			{
				Leak->Destroy();
			}
		}
		Leaks.Reset();
	}

	GetWorldTimerManager().ClearTimer(CameraCaptureTimer);

	Super::EndPlay(EndPlayReason);
}

void ASubmarine::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASubmarine, MoveState);
	DOREPLIFETIME(ASubmarine, TestStepIndex);
	DOREPLIFETIME(ASubmarine, WaterLevel);
	DOREPLIFETIME(ASubmarine, NumLeaks);
	DOREPLIFETIME(ASubmarine, bFlooded);
	DOREPLIFETIME(ASubmarine, SurfaceZ);
}

void ASubmarine::SetThrottle(float Value)
{
	if (HasAuthority() && !bFlooded && !bControlsLocked)
	{
		Throttle = FMath::Clamp(Value, -1.f, 1.f);
	}
}

void ASubmarine::SetRudder(float Value)
{
	if (HasAuthority() && !bFlooded && !bControlsLocked)
	{
		Rudder = FMath::Clamp(Value, -1.f, 1.f);
	}
}

void ASubmarine::SetDive(float Value)
{
	if (HasAuthority() && !bFlooded && !bControlsLocked)
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
		UpdateWater(DeltaSeconds);
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

	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateJuice(DeltaSeconds);
	}
}

void ASubmarine::UpdateJuice(float DeltaSeconds)
{
	// Alarm: red pulsing lights and the siren while water is coming in (or it's already too late)
	const bool bWantAlarm = NumLeaks > 0 || bFlooded;
	if (bWantAlarm != bAlarmOn)
	{
		bAlarmOn = bWantAlarm;
		AlarmTime = 0.f;
		if (bAlarmOn)
		{
			AlarmAudio->SetVolumeMultiplier(SirenVolume);
			AlarmAudio->Play();
		}
		else
		{
			AlarmAudio->Stop();
		}
	}

	FLinearColor LightColor = NormalLightColor;
	float LightIntensity = NormalLightIntensity;
	if (bAlarmOn)
	{
		AlarmTime += DeltaSeconds;
		const float Pulse = 0.5f + 0.5f * FMath::Sin(2.f * UE_PI * AlarmPulseRate * AlarmTime);
		LightColor = FLinearColor::Red;
		LightIntensity = FMath::Lerp(AlarmLightIntensity * 0.1f, AlarmLightIntensity, Pulse);
	}
	for (UPointLightComponent* Light : { HelmLight, SonarLight, EngineLight })
	{
		Light->SetLightColor(LightColor);
		Light->SetIntensity(LightIntensity);
	}

	// Creaks: the deeper, the louder. Each machine rolls its own timing; it's only atmosphere.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextCreakTime)
	{
		NextCreakTime = Now + FMath::FRandRange(CreakInterval.X, CreakInterval.Y);

		const float Depth = GetDepth();
		if (Depth >= CreakStartDepth && CreakSounds.Num() > 0)
		{
			const float Deepness = FMath::Clamp((Depth - CreakStartDepth) / FMath::Max(CreakFullDepth - CreakStartDepth, 1.f), 0.f, 1.f);
			const FVector Where = InteriorRoot->GetComponentTransform().TransformPosition(
				FVector(FMath::FRandRange(-0.5f, 0.5f) * InteriorSize.X, FMath::RandBool() ? InteriorSize.Y * 0.5f : -InteriorSize.Y * 0.5f, InteriorSize.Z * 0.7f));

			UGameplayStatics::PlaySoundAtLocation(this, CreakSounds[FMath::RandRange(0, CreakSounds.Num() - 1)], Where,
				FMath::Lerp(0.25f, 0.9f, Deepness), FMath::FRandRange(0.35f, 0.55f));
			ShakeLocalRiders(FMath::Lerp(0.1f, 0.5f, Deepness));
		}
	}
}

void ASubmarine::MulticastHullImpact_Implementation(float ImpactSpeed, FVector_NetQuantize Location)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// 0 = barely a bump, 1 = hard enough to spring a leak with room to spare
	const float Strength = FMath::Clamp((ImpactSpeed - MinImpactSpeed) / FMath::Max(LeakImpactSpeed * 1.5f - MinImpactSpeed, 1.f), 0.f, 1.f);
	PlayImpactFeedback(Strength, Location);
}

void ASubmarine::PlayImpactFeedback(float Strength, const FVector& Location)
{
	if (ImpactSound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Location, FMath::Lerp(0.3f, 1.f, Strength), FMath::Lerp(0.6f, 0.4f, Strength));
	}
	ShakeLocalRiders(FMath::Lerp(0.5f, 4.f, Strength));
}

void ASubmarine::ShakeLocalRiders(float Strength) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (PlayerController != nullptr && PlayerController->IsLocalController()
			&& PlayerController->GetPawn() != nullptr && IsInsideHull(PlayerController->GetPawn()->GetActorLocation()))
		{
			UHullShakeCameraModifier::Shake(PlayerController, Strength);
		}
	}
}

void ASubmarine::BuildSiren()
{
	// A two-tone wail sweeping 450 <-> 1100 Hz. The average pitch (775 Hz) times the period (1.6 s) is
	// a whole number of cycles, so the loop joins up without a click.
	constexpr int32 SampleRate = 22050;
	constexpr float Period = 1.6f;
	constexpr float BaseFrequency = 775.f;
	constexpr float Sweep = 325.f;
	const int32 NumSamples = FMath::RoundToInt(SampleRate * Period);

	SirenPcm = MakeShared<TArray<uint8>>();
	SirenPcm->SetNumUninitialized(NumSamples * sizeof(int16));
	int16* Samples = reinterpret_cast<int16*>(SirenPcm->GetData());

	double Phase = 0.0;
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const double T = static_cast<double>(Index) / SampleRate;
		const double Frequency = BaseFrequency - Sweep * FMath::Cos(2.0 * UE_DOUBLE_PI * T / Period);
		Phase += 2.0 * UE_DOUBLE_PI * Frequency / SampleRate;

		// A bit of second harmonic makes it sound like a speaker instead of a test tone
		const double Value = 0.8 * FMath::Sin(Phase) + 0.2 * FMath::Sin(2.0 * Phase);
		Samples[Index] = static_cast<int16>(FMath::Clamp(Value, -1.0, 1.0) * 0.6 * MAX_int16);
	}

	SirenWave = NewObject<USoundWaveProcedural>(this, TEXT("SirenWave"));
	SirenWave->SetSampleRate(SampleRate);
	SirenWave->NumChannels = 1;
	SirenWave->Duration = 10000.f; // INDEFINITELY_LOOPING_DURATION
	SirenWave->bLooping = false;

	// Called from the audio thread whenever it runs low; the buffer is never modified, so sharing it is safe
	TSharedPtr<TArray<uint8>> Pcm = SirenPcm;
	SirenWave->OnSoundWaveProceduralUnderflow.BindLambda([Pcm](USoundWaveProcedural* Wave, int32 SamplesRequired)
	{
		Wave->QueueAudio(Pcm->GetData(), Pcm->Num());
	});

	AlarmAudio->SetSound(SirenWave);
}

void ASubmarine::UpdateTestManeuvers(float DeltaSeconds)
{
	if (!bTestManeuvers || bAutopilotSuspended || bFlooded || bControlsLocked || TestManeuvers.Num() == 0)
	{
		if (TestStepIndex != INDEX_NONE)
		{
			TestStepIndex = INDEX_NONE;

			// Switched off mid-run: don't leave the sub driving on the last step's inputs.
			// Suspended means someone took the helm, so leave the inputs to them.
			if (!bAutopilotSuspended)
			{
				AllStop();
			}
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

void ASubmarine::UpdateCameraFeed()
{
	if (!HelmScreenComponent || !HelmScreenMesh)
		return;

	if (!HelmScreenMesh->WasRecentlyRendered(0.2f))
	{
		return;
	}

	HelmScreenComponent->CaptureScene();
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

	// Stations and anything else attached to us sit inside the hull box; don't let them block the sweep
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors, true, true);
	HullCollision->ClearMoveIgnoreActors();
	for (AActor* AttachedActor : AttachedActors)
	{
		HullCollision->IgnoreActorWhenMoving(AttachedActor, true);
	}

	// Players ride inside the hull. Ignoring the Pawn channel only covers their capsules; any extra
	// components on them (body meshes, held items) can be WorldDynamic and would block us otherwise.
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		HullCollision->IgnoreActorWhenMoving(*It, true);
	}

	FHitResult Hit;
	AddActorWorldOffset(Delta, true, &Hit);

	if (Hit.bStartPenetrating && Hit.PenetrationDepth > SubmarineInterior::MaxDepenetration)
	{
		UE_LOG(LogSubmarine, Warning, TEXT("[%s] Hull is %.0f cm inside %s (%s); not pushing out. Does it need NoCollision?"),
			HasAuthority() ? TEXT("Server") : TEXT("Client"), Hit.PenetrationDepth,
			*GetNameSafe(Hit.GetComponent()), *GetNameSafe(Hit.GetActor()));
	}
	else if (Hit.bStartPenetrating)
	{
		UE_LOG(LogSubmarine, Log, TEXT("[%s] Hull started move inside %s (depth %.2f cm), pushing out"),
			HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetNameSafe(Hit.GetComponent()), Hit.PenetrationDepth);

		AddActorWorldOffset(Hit.Normal * (Hit.PenetrationDepth + SubmarineInterior::DepenetrationPadding));
		AddActorWorldOffset(Delta, true, &Hit);

		if (Hit.bStartPenetrating)
		{
			UE_LOG(LogSubmarine, Warning, TEXT("[%s] Hull still stuck inside %s after pushing out"),
				HasAuthority() ? TEXT("Server") : TEXT("Client"), *GetNameSafe(Hit.GetComponent()));
		}
	}

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
			MulticastHullImpact(IntoSurface, Hit.ImpactPoint);

			if (IntoSurface >= LeakImpactSpeed)
			{
				SpawnLeakFromImpact(Hit);
			}
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
	float Yaw = MoveState.Yaw;
	FVector Location = MoveState.Location;

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
		// Big jumps (joining, debug teleports, round resets) carry our own character along
		TeleportWithRiders(Location, FRotator(CurrentRotation.Pitch, Yaw, CurrentRotation.Roll));
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