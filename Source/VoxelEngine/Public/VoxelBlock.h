// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelGlobalProperties.h"
#include "ProceduralMeshComponent.h"
#include "VoxelBlock.generated.h"

UCLASS()
class VOXELENGINE_API AVoxelBlock : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVoxelBlock();
	UPROPERTY(EditAnywhere)
	UProceduralMeshComponent* VoxelMesh;
	int32 VoxelSize = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Data")
	int32 X = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Data")
	int32 Y = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voxel Data")
	int32 Z = 0;


	//TArray<FVector> Vertices =
	//{
	//	// Bottom face (Z = -VoxelSize)
	//	FVector(-VoxelSize, -VoxelSize, -VoxelSize), // Vertex 0: Bottom-left-back corner
	//	FVector(VoxelSize, -VoxelSize, -VoxelSize),  // Vertex 1: Bottom-right-back corner
	//	FVector(VoxelSize,  VoxelSize, -VoxelSize),  // Vertex 2: Bottom-right-front corner
	//	FVector(-VoxelSize,  VoxelSize, -VoxelSize), // Vertex 3: Bottom-left-front corner

	//	// Top face (Z = VoxelSize)
	//	FVector(-VoxelSize, -VoxelSize,  VoxelSize), // Vertex 4: Top-left-back corner
	//	FVector(VoxelSize, -VoxelSize,  VoxelSize),  // Vertex 5: Top-right-back corner
	//	FVector(VoxelSize,  VoxelSize,  VoxelSize),  // Vertex 6: Top-right-front corner
	//	FVector(-VoxelSize,  VoxelSize,  VoxelSize)  // Vertex 7: Top-left-front corner
	//};

	TArray<int32> Triangles;
	//{
	//0, 1, 2, 0, 2, 3, // Bottom face
	//4, 6, 5, 4, 7, 6, // Top face
	//0, 4, 5, 0, 5, 1, // Front face
	//1, 5, 6, 1, 6, 2, // Right face
	//2, 6, 7, 2, 7, 3, // Back face
	//3, 7, 4, 3, 4, 0  // Left face
	//};

	TArray<FVector> Normals = 
	{
		// Bottom face (points downward)
		FVector(0.0f, 0.0f, -1.0f), FVector(0.0f, 0.0f, -1.0f),
		FVector(0.0f, 0.0f, -1.0f), FVector(0.0f, 0.0f, -1.0f),

		// Top face (points upward)
		FVector(0.0f, 0.0f, 1.0f), FVector(0.0f, 0.0f, 1.0f),
		FVector(0.0f, 0.0f, 1.0f), FVector(0.0f, 0.0f, 1.0f),

		// Front face (points forward)
		FVector(1.0f, 0.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f),
		FVector(1.0f, 0.0f, 0.0f), FVector(1.0f, 0.0f, 0.0f),

		// Right face (points right)
		FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f),

		// Back face (points backward)
		FVector(-1.0f, 0.0f, 0.0f), FVector(-1.0f, 0.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f), FVector(-1.0f, 0.0f, 0.0f),

		// Left face (points left)
		FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, -1.0f, 0.0f),
		FVector(0.0f, -1.0f, 0.0f), FVector(0.0f, -1.0f, 0.0f)
	};

	TArray<FVector2D> UV0 =
	{
		// Bottom face
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f),

		// Top face
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f),

		// Front face
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f),

		// Right face
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f),

		// Back face
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f),

		// Left face
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f),
		FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f)
	};

	TArray<FColor> VertexColors =
	{
	FColor::White, FColor::White, FColor::White, FColor::White,  // Bottom face
	FColor::White, FColor::White, FColor::White, FColor::White,  // Top face
	FColor::White, FColor::White, FColor::White, FColor::White,  // Front face
	FColor::White, FColor::White, FColor::White, FColor::White,  // Right face
	FColor::White, FColor::White, FColor::White, FColor::White,  // Back face
	FColor::White, FColor::White, FColor::White, FColor::White   // Left face
	};

	TArray<FProcMeshTangent> Tangents =
	{
		// Bottom face (tangent points along X axis)
		FProcMeshTangent(1.0f, 0.0f, 0.0f), FProcMeshTangent(1.0f, 0.0f, 0.0f),
		FProcMeshTangent(1.0f, 0.0f, 0.0f), FProcMeshTangent(1.0f, 0.0f, 0.0f),

		// Top face (tangent points along X axis)
		FProcMeshTangent(1.0f, 0.0f, 0.0f), FProcMeshTangent(1.0f, 0.0f, 0.0f),
		FProcMeshTangent(1.0f, 0.0f, 0.0f), FProcMeshTangent(1.0f, 0.0f, 0.0f),

		// Front face (tangent points along Y axis)
		FProcMeshTangent(0.0f, 1.0f, 0.0f), FProcMeshTangent(0.0f, 1.0f, 0.0f),
		FProcMeshTangent(0.0f, 1.0f, 0.0f), FProcMeshTangent(0.0f, 1.0f, 0.0f),

		// Right face (tangent points along Z axis)
		FProcMeshTangent(0.0f, 0.0f, 1.0f), FProcMeshTangent(0.0f, 0.0f, 1.0f),
		FProcMeshTangent(0.0f, 0.0f, 1.0f), FProcMeshTangent(0.0f, 0.0f, 1.0f),

		// Back face (tangent points along Y axis)
		FProcMeshTangent(0.0f, 1.0f, 0.0f), FProcMeshTangent(0.0f, 1.0f, 0.0f),
		FProcMeshTangent(0.0f, 1.0f, 0.0f), FProcMeshTangent(0.0f, 1.0f, 0.0f),

		// Left face (tangent points along Z axis)
		FProcMeshTangent(0.0f, 0.0f, 1.0f), FProcMeshTangent(0.0f, 0.0f, 1.0f),
		FProcMeshTangent(0.0f, 0.0f, 1.0f), FProcMeshTangent(0.0f, 0.0f, 1.0f)
	};

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	void UpdateVoxelFace(int32 face);
};
