#include "FlyCameraPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Math/RotationMatrix.h"
#include "Voxel.h"

AFlyCameraPawn::AFlyCameraPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(RootScene);
	CameraComponent->bUsePawnControlRotation = true;

	MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComponent"));
	MovementComponent->SetUpdatedComponent(RootScene);
	MovementComponent->Acceleration = 50000.0f;
	MovementComponent->Deceleration = 50000.0f;
	MovementComponent->TurningBoost = 8.0f;

	InitialMoveSpeed = 3000.0f;
	MinMoveSpeed = 250.0f;
	MaxMoveSpeed = 20000.0f;
	SpeedStepMultiplier = 1.2f;
	LookSensitivity = 1.0f;

	bUseControllerRotationPitch = true;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;
}

void AFlyCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	ApplyMoveSpeed();
	ConfigurePlayerController();
}

void AFlyCameraPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	ConfigurePlayerController();
}

void AFlyCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("FlyForward"), this, &AFlyCameraPawn::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("FlyRight"), this, &AFlyCameraPawn::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("FlyUp"), this, &AFlyCameraPawn::MoveUp);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AFlyCameraPawn::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AFlyCameraPawn::LookUp);
	PlayerInputComponent->BindAxis(TEXT("FlySpeed"), this, &AFlyCameraPawn::AdjustSpeed);
	PlayerInputComponent->BindAction(TEXT("VoxelToggleStatsDetail"), IE_Pressed, this, &AFlyCameraPawn::OnToggleVoxelStatsDetail);
}

void AFlyCameraPawn::MoveForward(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator ControlRotation = Controller ? Controller->GetControlRotation() : GetActorRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);
	const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	AddMovementInput(Direction, Value);
}

void AFlyCameraPawn::MoveRight(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	const FRotator ControlRotation = Controller ? Controller->GetControlRotation() : GetActorRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);
	const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(Direction, Value);
}

void AFlyCameraPawn::MoveUp(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	AddMovementInput(FVector::UpVector, Value);
}

void AFlyCameraPawn::Turn(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	AddControllerYawInput(Value * LookSensitivity);
}

void AFlyCameraPawn::LookUp(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	AddControllerPitchInput(-Value * LookSensitivity);
}

void AFlyCameraPawn::AdjustSpeed(float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	InitialMoveSpeed = Value > 0.0f
		? InitialMoveSpeed * SpeedStepMultiplier
		: InitialMoveSpeed / SpeedStepMultiplier;

	InitialMoveSpeed = FMath::Clamp(InitialMoveSpeed, MinMoveSpeed, MaxMoveSpeed);
	ApplyMoveSpeed();
}

void AFlyCameraPawn::ApplyMoveSpeed()
{
	if (MovementComponent)
	{
		MovementComponent->MaxSpeed = InitialMoveSpeed;
	}
}

void AFlyCameraPawn::ConfigurePlayerController()
{
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->bShowMouseCursor = false;
	}
}

void AFlyCameraPawn::OnToggleVoxelStatsDetail()
{
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Voxel.StatsOverlayMode")))
	{
		CV->Set(CV->GetInt() == 0 ? 1 : 0, ECVF_SetByCode);
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AVoxel> It(World); It; ++It)
		{
			It->ForceRefreshGenerationStatsDisplay();
			break;
		}
	}
}
