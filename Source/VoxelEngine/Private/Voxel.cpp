#include "Voxel.h"

// Constructor
AVoxel::AVoxel()
	: NumChunks(4), ChunkGridSize(32), VoxelSize(100.f)
{
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void AVoxel::BeginPlay()
{
	Super::BeginPlay();
	CreateChunks();
}

// Create chunks
void AVoxel::CreateChunks()
{
	TasksCompleted.Reset();
	int32 totalTasks = NumChunks * NumChunks; // Assuming a 2D grid of chunks
	TasksCompleted.Add(totalTasks);

	for (int32 x = 0; x < NumChunks; x++)
	{
		for (int32 y = 0; y < NumChunks; y++)
		{
			// Calculate the global offset for this chunk
			FVector2D ChunkOffset = FVector2D(x * ChunkGridSize, y * ChunkGridSize);

			// Create and initialize chunk
			AVoxelChunk* NewChunk = GetWorld()->SpawnActor<AVoxelChunk>();
			FVector ChunkPosition = FVector(x * ChunkGridSize * VoxelSize * 2, y * ChunkGridSize * VoxelSize * 2, 0);
			NewChunk->Initialize(ChunkGridSize, VoxelSize, ChunkPosition, ChunkOffset);

			VoxelChunks.Add(NewChunk);

			// Launch multithreaded tasks for chunk calculation
			AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, NewChunk]()
				{
					NewChunk->CalculateVoxels(0, ChunkGridSize);

					// Once the task completes, decrement the counter
					if (TasksCompleted.Decrement() == 0)
					{
						// Spawn voxel blocks once all tasks are done
						AsyncTask(ENamedThreads::GameThread, [this]()
							{
								for (AVoxelChunk* Chunk : VoxelChunks)
								{
									Chunk->SpawnVoxelBlocks();
								}
							});
					}
				});
		}
	}
}

