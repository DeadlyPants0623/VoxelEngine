// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Misc/Optional.h"
#include "ProceduralMeshComponent.h"
#include "VoxelTerrainSettings.h"
#include "VoxelChunk.generated.h"

struct FVoxelChunkMeshStats
{
	int32 SolidVoxelCount = 0;
	int32 SurfaceVoxelCount = 0;
	int32 CulledInteriorVoxelCount = 0;
	int32 FaceCount = 0;
	int32 TriangleCount = 0;
	int32 VertexCount = 0;
};

struct FVoxelChunkBorderData
{
	int32 GridSize = 0;
	int32 ChunkHeight = 0;
	TArray<uint8> PositiveX;
	TArray<uint8> NegativeX;
	TArray<uint8> PositiveY;
	TArray<uint8> NegativeY;

	void Reset(int32 InGridSize, int32 InChunkHeight);
	bool HasData() const;
	bool IsPositiveXSolid(int32 Y, int32 Z) const;
	bool IsNegativeXSolid(int32 Y, int32 Z) const;
	bool IsPositiveYSolid(int32 X, int32 Z) const;
	bool IsNegativeYSolid(int32 X, int32 Z) const;
};

struct FVoxelChunkNeighborBorderData
{
	TOptional<FVoxelChunkBorderData> PositiveX;
	TOptional<FVoxelChunkBorderData> NegativeX;
	TOptional<FVoxelChunkBorderData> PositiveY;
	TOptional<FVoxelChunkBorderData> NegativeY;
};

struct FVoxelChunkMeshData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	FVoxelChunkMeshStats Stats;

	void Reset();
	bool HasGeometry() const;
};

UCLASS()
class VOXELENGINE_API AVoxelChunk : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AVoxelChunk();
	
	enum EBlockType : uint8
	{
		Air,
		Dirt,
		Grass,
		Stone,
		Water
	};

	struct FVoxel
	{
		uint16 Height = 0;
		EBlockType BlockType = Air;
	};

	// Initialize the chunk with the required parameters
	void Initialize(int32 InGridSize, float InVoxelSize, FVector ChunkPosition, FVector2D ChunkOffset, int32 ChunkHeight);

	// Generate voxel data on the chunk itself.
	void CalculateVoxels(int32 startX, int32 endX, const FVoxelTerrainSettings& InTerrainSettings, int32 InWorldSeed = 0);

	// Generate voxel data without touching actor state.
	static void BuildVoxelData(
		TArray<FVoxel>& OutVoxels,
		FVoxelTerrainSampleStats& OutTerrainStats,
		FVoxelChunkBorderData& OutBorderData,
		int32 InGridSize,
		int32 InChunkHeight,
		const FVector2D& InChunkOffset,
		const FVoxelTerrainSettings& InTerrainSettings,
		int32 InWorldSeed);

	static void BuildBorderData(
		FVoxelChunkBorderData& OutBorderData,
		const TArray<FVoxel>& InVoxels,
		int32 InGridSize,
		int32 InChunkHeight);

	static void BuildMeshData(
		FVoxelChunkMeshData& OutMeshData,
		const TArray<FVoxel>& InVoxels,
		int32 InGridSize,
		int32 InChunkHeight,
		float InVoxelSize,
		const FVoxelChunkNeighborBorderData& NeighborBorderData);

	void SetVoxelData(TArray<FVoxel>&& InVoxels);
	void ApplyMeshData(const FVoxelChunkMeshData& InMeshData);
	void SetChunkCollisionEnabled(bool bShouldEnableCollision, const FVoxelChunkMeshData* InMeshData = nullptr);
	bool IsChunkCollisionEnabled() const { return bCollisionEnabled; }
	void SetChunkRendered(bool bShouldRender);

	// Spawn voxel blocks after all chunks are generated
	void SpawnVoxelBlocks();

	// Clear all voxels
	void DestroyVoxels();

	const FVoxelChunkMeshStats& GetMeshStats() const { return MeshStats; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Mesh")
	UProceduralMeshComponent* ChunkMesh;

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

	// Offset to represent the chunk's global position for continuous noise
	FVector2D ChunkOffset;

	UMaterialInterface* DirtMaterial = nullptr;
	FVoxelChunkMeshStats MeshStats;
	bool bCollisionEnabled = true;
};
