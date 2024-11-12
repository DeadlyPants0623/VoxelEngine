#include "VoxelChunk.h"
#include "Math/UnrealMathUtility.h"

// Constructor
AVoxelChunk::AVoxelChunk()
	: GridSize(0), VoxelSize(0)
{
	PrimaryActorTick.bCanEverTick = false;

	// Initialize the HISM component
	VoxelHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("VoxelHISM"));
	RootComponent = VoxelHISM;

	// You need to set a static mesh for HISM
	static ConstructorHelpers::FObjectFinder<UStaticMesh> VoxelMeshAsset(TEXT("/Game/StarterContent/Shapes/Shape_Cube.Shape_Cube"));
	if (VoxelMeshAsset.Succeeded())
	{
		VoxelHISM->SetStaticMesh(VoxelMeshAsset.Object);
	}

	VoxelHISM->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	VoxelHISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VoxelHISM->SetCullDistance(0);

	static ConstructorHelpers::FObjectFinder<UMaterialInstance> DirtMaterialAsset(TEXT("/Game/Megascans/Surfaces/Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs/MI_Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs_8K.MI_Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs_8K"));
	if (DirtMaterialAsset.Succeeded())
	{
		DirtMaterial = DirtMaterialAsset.Object;
	}
}

// Called when the game starts or when spawned
void AVoxelChunk::BeginPlay()
{
	Super::BeginPlay();
}

void AVoxelChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Helper to get the index of a voxel in the array
int32 AVoxelChunk::GetVoxelIndex(int32 x, int32 y, int32 z) const
{
    return x + (y * GridSize) + (z * GridSize * GridSize);
}

// Initialize the chunk with grid size, voxel size, and chunk position
void AVoxelChunk::Initialize(int32 InGridSize, float InVoxelSize, FVector InChunkPosition, FVector2D InChunkOffset)
{
	GridSize = InGridSize;
	VoxelSize = InVoxelSize;
	ChunkPosition = InChunkPosition;
	ChunkOffset = InChunkOffset;

	// Set voxel array size
	Voxels.SetNum(GridSize * GridSize * GridSize);
	VoxelInstanceIndices.SetNum(GridSize * GridSize * GridSize);  // Ensure array has correct size
}

// Generate voxel data for this chunk (using global coordinates)
void AVoxelChunk::CalculateVoxels(int32 startX, int32 endX)
{
	for (int32 x = startX; x < endX; x++)
	{
		for (int32 y = 0; y < GridSize; y++)
		{
			// Calculate global coordinates for Perlin noise
			float GlobalX = ChunkOffset.X + x;
			float GlobalY = ChunkOffset.Y + y;

			// Generate continuous height using Perlin noise
			float noiseValue = FMath::PerlinNoise2D(FVector2D(GlobalX, GlobalY) * PerlinScale);
			int32 height = FMath::Clamp(FMath::RoundToInt(noiseValue * GridSize), 0, GridSize - 1);

			for (int32 z = 0; z < GridSize; z++)
			{
				int32 index = x + (y * GridSize) + (z * GridSize * GridSize);
				FVoxel voxel;
				voxel.X = x;
				voxel.Y = y;
				voxel.Z = z;
				voxel.IsSolid = z <= height;
				voxel.BlockType = voxel.IsSolid ? EBlockType::Dirt : EBlockType::Air;
				Voxels[index] = voxel;
			}
		}
	}
}

// Spawns voxel blocks for the chunk
void AVoxelChunk::SpawnVoxelBlocks()
{
	for (FVoxel& voxel : Voxels)
	{
		if (voxel.IsSolid)
		{
			// Add the voxel instance to the HISM
			AddVoxelInstance(voxel.X, voxel.Y, voxel.Z);
			//// Calculate the world position of the voxel block, adjusting for the chunk's position
			//FVector VoxelWorldPosition = FVector(voxel.X * VoxelSize * 2, voxel.Y * VoxelSize * 2, voxel.Z * VoxelSize * 2) + ChunkPosition;

			//// Set spawn parameters
			//FActorSpawnParameters SpawnParams;
			//SpawnParams.Owner = this;  // The chunk is the owner
			//SpawnParams.Instigator = GetInstigator();

			//// Spawn the voxel block at the calculated world position
			//FRotator SpawnRotation = FRotator::ZeroRotator;
			//AVoxelBlock* VoxelBlock = GetWorld()->SpawnActor<AVoxelBlock>(VoxelWorldPosition, SpawnRotation, SpawnParams);

			//if (VoxelBlock)
			//{
				// Set the properties of the voxel block (such as size and grid coordinates)
				//VoxelBlock->VoxelSize = VoxelSize;
				//VoxelBlock->X = voxel.X;
				//VoxelBlock->Y = voxel.Y;
				//VoxelBlock->Z = voxel.Z;

				// Check and show the appropriate voxel faces based on neighbors
				// Use a method like CheckAllNeighbors to determine which faces should be shown
				//CheckAllNeighbors(voxel, VoxelBlock, voxel.X, voxel.Y, voxel.Z);

				

			//}
		}
	}
}

void AVoxelChunk::CheckAllNeighbors(FVoxel& voxel, AVoxelBlock* VoxelBlock, int32 x, int32 y, int32 z)
{
	UE_LOG(LogTemp, Display, TEXT("Check Start"));
	//Check neighbors and show faces if the neighbor is solid
	if (!CheckNeighbor(voxel.X, voxel.Y, voxel.Z - 1)) // Check below in Z
	{
		UE_LOG(LogTemp, Warning, TEXT("Show Bottom Face"));
		VoxelBlock->UpdateVoxelFace(1); // Bottom face
	}
	if (!CheckNeighbor(voxel.X, voxel.Y, voxel.Z + 1)) // Check above in Z
	{
		UE_LOG(LogTemp, Warning, TEXT("Show Top Face"));
		VoxelBlock->UpdateVoxelFace(0); // Top face
	}
	if (!CheckNeighbor(voxel.X - 1, voxel.Y, voxel.Z)) // Check front in Y
	{
		UE_LOG(LogTemp, Warning, TEXT("Show Front Face"));
		VoxelBlock->UpdateVoxelFace(2); // Front face
	}
	if (!CheckNeighbor(voxel.X + 1, voxel.Y, voxel.Z)) // Check back in Y
	{
		UE_LOG(LogTemp, Warning, TEXT("Show Back Face"));
		VoxelBlock->UpdateVoxelFace(3); // Back face
	}
	if (!CheckNeighbor(voxel.X, voxel.Y + 1, voxel.Z)) // Check left in X
	{
		UE_LOG(LogTemp, Warning, TEXT("Show Left Face"));
		VoxelBlock->UpdateVoxelFace(4); // Left face
	}
	if (!CheckNeighbor(voxel.X, voxel.Y - 1, voxel.Z)) // Check right in X
	{
		UE_LOG(LogTemp, Warning, TEXT("Show Right Face"));
		VoxelBlock->UpdateVoxelFace(5); // Right face
	}
	UE_LOG(LogTemp, Display, TEXT("Check Finish"));
}


bool AVoxelChunk::CheckNeighbor(int32 x, int32 y, int32 z)
{
	UE_LOG(LogTemp, Warning, TEXT("Checking neighbor at (%d, %d, %d)"), x, y, z);

	// Check that the position is within bounds
	if (x < 0 || x >= GridSize || y < 0 || y >= GridSize || z < 0 || z >= GridSize)
	{
		return false; // Out of bounds is considered not solid
	}

	int32 index = x + (y * GridSize) + (z * GridSize * GridSize);
	bool isSolid = Voxels[index].IsSolid;
	UE_LOG(LogTemp, Warning, TEXT("Neighbor solid state: %d"), isSolid);
	return isSolid;
}

void AVoxelChunk::AddVoxelInstance(int32 x, int32 y, int32 z)
{
	// Calculate the index in the voxel instance array
	int32 VoxelArrayIndex = GetVoxelIndex(x, y, z);

	// Check if the index is within the bounds of the array
	if (!VoxelInstanceIndices.IsValidIndex(VoxelArrayIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid VoxelArrayIndex: %d for voxel (%d, %d, %d)"), VoxelArrayIndex, x, y, z);
		return;
	}

	// Calculate the world position of the voxel block
	FVector VoxelWorldPosition = FVector(x * VoxelSize * 2, y * VoxelSize * 2, z * VoxelSize * 2) + ChunkPosition;

	// Create the transform for the voxel block
	FTransform VoxelTransform(FRotator::ZeroRotator, VoxelWorldPosition, FVector(1.7f, 1.7f, 1.7f));
	UE_LOG(LogTemp, Log, TEXT("VoxelTransform: %s"), *VoxelTransform.ToString());

	// Add the instance to the HISM and store its index
	int32 InstanceIndex = VoxelHISM->AddInstance(VoxelTransform, true);

	// Create a dynamic material instance based on BlockType
	UMaterialInstanceDynamic* DynamicMaterialInstance = nullptr;
	switch (Voxels[VoxelArrayIndex].BlockType)
	{
	case EBlockType::Dirt:
		DynamicMaterialInstance = UMaterialInstanceDynamic::Create(DirtMaterial, this);
		break;
	case EBlockType::Air:
		//DynamicMaterialInstance = UMaterialInstanceDynamic::Create(AirMaterial, this);
		break;
		// Add more cases for other BlockTypes
	default:
		break;
	}

	// Assign the dynamic material instance to the HISM instance
	if (DynamicMaterialInstance)
	{
		VoxelHISM->SetMaterial(InstanceIndex, DynamicMaterialInstance);
	}

	// Store the instance index in the array
	VoxelInstanceIndices[VoxelArrayIndex] = InstanceIndex;
}


void AVoxelChunk::RemoveVoxelInstance(int32 x, int32 y, int32 z)
{
	// Get the index of the voxel in the instance array
	int32 VoxelArrayIndex = GetVoxelIndex(x, y, z);
	int32 InstanceIndex = VoxelInstanceIndices[VoxelArrayIndex];

	// Remove the instance from the HISM
	if (InstanceIndex != INDEX_NONE)
	{
		VoxelHISM->RemoveInstance(InstanceIndex);
		VoxelInstanceIndices[VoxelArrayIndex] = INDEX_NONE;  // Mark as removed
	}
}

// Clear voxel data
void AVoxelChunk::DestroyVoxels()
{
	Voxels.Empty();
	// Clear all voxel instances from the HISM
	VoxelHISM->ClearInstances();
	VoxelInstanceIndices.Empty();
}
