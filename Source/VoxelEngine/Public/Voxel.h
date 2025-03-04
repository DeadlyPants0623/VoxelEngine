#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelChunk.h"  // Include the chunk class
#include "Voxel.generated.h"

UCLASS()
class VOXELENGINE_API AVoxel : public AActor
{
	GENERATED_BODY()

public:
	AVoxel();

	// Number of chunks
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 NumChunks;

	// Chunk size (number of voxels per chunk)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 ChunkGridSize;

	// Voxel size
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	float VoxelSize;

	// Chunk height
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 ChunkHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	FTimespan GenerationTime;

protected:
	virtual void BeginPlay() override;

private:

	// Array of voxel chunks
	TArray<AVoxelChunk*> VoxelChunks;

	// Create chunks
	void CreateChunks();

	//void GenerateChunkMeshes();

	// Tasks for multithreading
	FThreadSafeCounter TasksCompleted;
};
