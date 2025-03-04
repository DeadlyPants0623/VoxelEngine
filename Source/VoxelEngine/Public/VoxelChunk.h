// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VoxelBlock.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "VoxelChunk.generated.h"

UCLASS()
class VOXELENGINE_API AVoxelChunk : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVoxelChunk();
	
	enum EBlockType
	{
		Air,
		Dirt,
		Grass,
		Stone,
		Water
	};

	struct FVoxel
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;
		bool IsSolid = false;
		EBlockType BlockType;
	};

	// Initialize the chunk with the required parameters
	void Initialize(int32 InGridSize, float InVoxelSize, FVector ChunkPosition, FVector2D ChunkOffset, int32 ChunkHeight);

	// Generate voxel data
	void CalculateVoxels(int32 startX, int32 endX);

	// Spawn voxel blocks after all chunks are generated
	void SpawnVoxelBlocks();

	// Clear all voxels
	void DestroyVoxels();

	// Add a voxel instance to the HISM
	void AddVoxelInstance(int32 x, int32 y, int32 z);

	// Remove a voxel instance from the HISM
	void RemoveVoxelInstance(int32 x, int32 y, int32 z);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Mesh")
	UStaticMesh* VoxelMesh;

	// Hierarchical Instanced Static Mesh component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Mesh")
	UHierarchicalInstancedStaticMeshComponent* VoxelHISM;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:

	// Voxel data storage
	TArray<FVoxel> Voxels;

	// Chunk grid size and voxel size
	int32 GridSize;
	float VoxelSize;
	int32 ChunkHeight;

	// Position of the chunk in world space
	FVector ChunkPosition;

	// Perlin noise scale
	float PerlinScale = 0.1f;

	// Offset to represent the chunk's global position for continuous noise
	FVector2D ChunkOffset;

	// Array of instance indices for each Chunk
	TArray<int32> VoxelInstanceIndices;

	// Helper to get the index of a voxel in the array
	int32 GetVoxelIndex(int32 x, int32 y, int32 z) const;

	UMaterialInstance* DirtMaterial;
};
