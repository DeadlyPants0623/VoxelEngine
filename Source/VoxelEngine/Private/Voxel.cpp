#include "Voxel.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"

// Constructor
AVoxel::AVoxel()
	: NumChunks(4), ChunkGridSize(32), VoxelSize(100.f), ChunkHeight(256)
{
	PrimaryActorTick.bCanEverTick = false;

	// Register Console Command
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("GenerateChunks"),       // Command name in console
		TEXT("Generates the chunks"), // Help description
		FConsoleCommandDelegate::CreateUObject(this, &AVoxel::CreateChunks),
		ECVF_Cheat                     // Flag indicating command type (Cheat, Development, etc.)
	);
}

// Called when the game starts or when spawned
void AVoxel::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Display, TEXT("Configurations :"));
	UE_LOG(LogTemp, Display, TEXT("Number of Chunks : %d | Number of Grid Per Chunk : %d"), NumChunks, ChunkGridSize );
	UE_LOG(LogTemp, Display, TEXT("Total Number of Voxels : %d"), ChunkGridSize * ChunkGridSize * ChunkGridSize * NumChunks);
	//CreateChunks();
}

// Create chunks
void AVoxel::CreateChunks()
{
	TasksCompleted.Reset();
	int32 totalTasks = NumChunks * NumChunks; // Assuming a 2D grid of chunks
	TasksCompleted.Add(totalTasks);

	FDateTime StartTime = FDateTime::Now();

	for (int32 x = 0; x < NumChunks; x++)
	{
		for (int32 y = 0; y < NumChunks; y++)
		{
			// Calculate the global offset for this chunk
			FVector2D ChunkOffset = FVector2D(x * ChunkGridSize, y * ChunkGridSize);

			// Create and initialize chunk
			AVoxelChunk* NewChunk = GetWorld()->SpawnActor<AVoxelChunk>();
			FVector ChunkPosition = FVector(x * ChunkGridSize * VoxelSize * 2, y * ChunkGridSize * VoxelSize * 2, 0);
			NewChunk->Initialize(ChunkGridSize, VoxelSize, ChunkPosition, ChunkOffset, ChunkHeight);

			VoxelChunks.Add(NewChunk);

			// Launch multithreaded tasks for chunk calculation
			AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, NewChunk, StartTime]()
				{
					UE_LOG(LogTemp, Display, TEXT("Background Thread: %d"), FPlatformProcess::GetCurrentProcessId());
					NewChunk->CalculateVoxels(0, ChunkGridSize);

					// Once the task completes, decrement the counter
					if (TasksCompleted.Decrement() == 0)
					{
						// Spawn voxel blocks once all tasks are done
						AsyncTask(ENamedThreads::GameThread, [this, StartTime]()
							{
								UE_LOG(LogTemp, Display, TEXT("Background Thread: %d"), FPlatformProcess::GetCurrentProcessId());
								for (AVoxelChunk* Chunk : VoxelChunks)
								{
									Chunk->SpawnVoxelBlocks();
								}

								FDateTime EndTime = FDateTime::Now();
								FTimespan Duration = EndTime - StartTime;
								GenerationTime = Duration;
								UE_LOG(LogTemp, Log, TEXT("Chunk creation took %s seconds"), *Duration.ToString());
							});
					}
				});
		}
	}
}
