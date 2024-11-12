// Fill out your copyright notice in the Description page of Project Settings.


#include "AvatarBase.h"
#include "GameFramework/CharacterMovementComponent.h"

// Sets default values
AAvatarBase::AAvatarBase()
{
	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// Initialize SprintSpeed
	SprintSpeed = 600.0f; // Set a default value for SprintSpeed

	// Set the character's max walk speed to SprintSpeed
	GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;

	// Set the character's walk speed
	WalkSpeed = 300.0f;

	// Set the turn rate for the character
	BaseTurnRate = 45.0f;

	// Set the look up rate for the character
	BaseLookUpRate = 45.0f;
}

// Called when the game starts or when spawned
void AAvatarBase::BeginPlay()
{
	Super::BeginPlay();

	// Add the input mapping context
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputMapping, 0);
		}
	}
}

// Called every frame
void AAvatarBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	GetLookAt();

}

// Called to bind functionality to input
void AAvatarBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Bind the input actions to the input component
		EnhancedInputComponent->BindAction(MoveForwardAction, ETriggerEvent::Triggered, this, &AAvatarBase::MoveForward);
		EnhancedInputComponent->BindAction(MoveRightAction, ETriggerEvent::Triggered, this, &AAvatarBase::MoveRight);
		EnhancedInputComponent->BindAction(TurnAction, ETriggerEvent::Triggered, this, &AAvatarBase::Turn);
		EnhancedInputComponent->BindAction(LeftClickAction, ETriggerEvent::Triggered, this, &AAvatarBase::LeftClick);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &AAvatarBase::Jump);
		EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Triggered, this, &AAvatarBase::Sprint);
	}

}

// Called when the player presses the forward key
void AAvatarBase::MoveForward(const FInputActionValue& Value)
{
	// Get the player's forward vector
	FVector forward = GetActorForwardVector();

	// Move the player forward
	AddMovementInput(forward, Value.Get<float>());
}

// Called when the player presses the right key
void AAvatarBase::MoveRight(const FInputActionValue& Value)
{
	// Get the player's right vector
	FVector right = GetActorRightVector();

	// Move the player right
	AddMovementInput(right, Value.Get<float>());
}

// Called when the player presses the turn key
void AAvatarBase::Turn(const FInputActionValue& Value)
{
	// Get the turn input value (Axis2D)
	FVector2D TurnValue = Value.Get<FVector2D>();

	// Apply the turn rate to the yaw rotation
	AddControllerYawInput(TurnValue.X * BaseTurnRate * GetWorld()->GetDeltaSeconds());
	// Apply the look up rate to the pitch rotation
	AddControllerPitchInput(-TurnValue.Y * BaseLookUpRate * GetWorld()->GetDeltaSeconds());

}

// Called when the player presses the jump key
void AAvatarBase::Jump()
{
	// Jump
	Super::Jump();
}

// Called when the player presses the sprint key
void AAvatarBase::Sprint(const FInputActionValue& Value)
{
	// If the player is sprinting
	if (Value.Get<bool>())
	{
		// Set the character's max walk speed to SprintSpeed
		GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
	}
	else
	{
		// Set the character's max walk speed to WalkSpeed
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	}
}

// Called when the player presses the left click key
void AAvatarBase::LeftClick(const FInputActionValue& Value)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Left Click"));


}


void AAvatarBase::GetLookAt()
{
	// Get the player controller
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController)
	{
		return;
	}

	// Get the camera location and forward vector
	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	FVector ForwardVector = CameraRotation.Vector();

	// Calculate the end location based on the camera's forward vector
	FVector End = CameraLocation + (ForwardVector * 1000.0f);

	// Create a collision query
	FCollisionQueryParams CollisionParams;
	CollisionParams.AddIgnoredActor(this);

	// Create a hit result
	FHitResult HitResult;

	// Perform the line trace
	GetWorld()->LineTraceSingleByChannel(HitResult, CameraLocation, End, ECollisionChannel::ECC_WorldDynamic, CollisionParams);
	DrawDebugLine(GetWorld(), CameraLocation, End, FColor::Red, false, 1.0f, 0, 1.0f);

	// If the line trace hits something
	if (HitResult.bBlockingHit)
	{
		// Get the hit location
		FVector HitLocation = HitResult.ImpactPoint;
		TracedLocation = HitLocation;

		// Get the hit actor
		AActor* HitActor = HitResult.GetActor();

		// If the hit actor is valid
		if (HitActor)
		{
			// Get the hit actor's name
			FString HitActorName = HitActor->GetName();

			// Print the hit actor's name
			UE_LOG(LogTemp, Warning, TEXT("Hit Actor: %s"), *HitActorName);
		}

		// Print the hit location
		UE_LOG(LogTemp, Warning, TEXT("Hit Location: %s"), *HitLocation.ToString());
	}
}

void AAvatarBase::RemoveBlock()
{

}