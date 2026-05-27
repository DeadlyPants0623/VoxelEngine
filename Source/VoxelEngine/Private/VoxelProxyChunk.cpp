#include "VoxelProxyChunk.h"

#include "UObject/ConstructorHelpers.h"

AVoxelProxyChunk::AVoxelProxyChunk()
{
	PrimaryActorTick.bCanEverTick = false;

	ProxyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProxyMesh"));
	RootComponent = ProxyMesh;
	ProxyMesh->bUseAsyncCooking = true;
	ProxyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProxyMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DirtMaterialAsset(TEXT("/Game/Megascans/Surfaces/Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs/MI_Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs_8K.MI_Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs_8K"));
	if (DirtMaterialAsset.Succeeded())
	{
		DirtMaterial = DirtMaterialAsset.Object;
	}
}

void AVoxelProxyChunk::BeginPlay()
{
	Super::BeginPlay();
}

void AVoxelProxyChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AVoxelProxyChunk::Initialize(const FVector& InWorldLocation)
{
	SetActorLocation(InWorldLocation);
	MeshStats = FVoxelChunkMeshStats{};
}

void AVoxelProxyChunk::ApplyMeshData(const FVoxelChunkMeshData& InMeshData)
{
	if (InMeshData.HasGeometry())
	{
		ProxyMesh->CreateMeshSection(0, InMeshData.Vertices, InMeshData.Triangles, InMeshData.Normals, InMeshData.UVs, InMeshData.VertexColors, InMeshData.Tangents, false);

		if (DirtMaterial != nullptr)
		{
			ProxyMesh->SetMaterial(0, DirtMaterial);
		}
	}
	else
	{
		ProxyMesh->ClearMeshSection(0);
	}

	MeshStats = InMeshData.Stats;
}

void AVoxelProxyChunk::SetProxyRendered(bool bShouldRender)
{
	if (ProxyMesh == nullptr)
	{
		return;
	}

	ProxyMesh->SetVisibility(bShouldRender, true);
	ProxyMesh->SetHiddenInGame(!bShouldRender, true);
}
