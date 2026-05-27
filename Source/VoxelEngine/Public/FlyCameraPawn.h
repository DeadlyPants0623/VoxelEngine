#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FlyCameraPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USceneComponent;

UCLASS()
class VOXELENGINE_API AFlyCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AFlyCameraPawn();

protected:
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UFloatingPawnMovement> MovementComponent;

	UPROPERTY(EditAnywhere, Category = "Fly Camera", meta = (ClampMin = "100.0"))
	float InitialMoveSpeed;

	UPROPERTY(EditAnywhere, Category = "Fly Camera", meta = (ClampMin = "100.0"))
	float MinMoveSpeed;

	UPROPERTY(EditAnywhere, Category = "Fly Camera", meta = (ClampMin = "100.0"))
	float MaxMoveSpeed;

	UPROPERTY(EditAnywhere, Category = "Fly Camera", meta = (ClampMin = "1.01"))
	float SpeedStepMultiplier;

	UPROPERTY(EditAnywhere, Category = "Fly Camera")
	float LookSensitivity;

	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void AdjustSpeed(float Value);
	void ApplyMoveSpeed();
	void ConfigurePlayerController();
	void OnToggleVoxelStatsDetail();
};
