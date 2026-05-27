#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "VoxelChunk.h"
#include "VoxelProxyChunk.generated.h"

UCLASS()
class VOXELENGINE_API AVoxelProxyChunk : public AActor
{
	GENERATED_BODY()

public:
	AVoxelProxyChunk();

	void Initialize(const FVector& InWorldLocation);
	void ApplyMeshData(const FVoxelChunkMeshData& InMeshData);
	void SetProxyRendered(bool bShouldRender);

	const FVoxelChunkMeshStats& GetMeshStats() const { return MeshStats; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Proxy")
	UProceduralMeshComponent* ProxyMesh;

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	UMaterialInterface* DirtMaterial = nullptr;
	FVoxelChunkMeshStats MeshStats;
};
