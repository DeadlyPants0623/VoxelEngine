// Fill out your copyright notice in the Description page of Project Settings.

#include "VoxelBlock.h"

// Sets default values
AVoxelBlock::AVoxelBlock()
{
    // Set this actor to call Tick() every frame. You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;

    VoxelMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("VoxelMesh"));
    RootComponent = VoxelMesh;
    VoxelMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	VoxelMesh->SetCollisionObjectType(ECC_WorldDynamic);
	VoxelMesh->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    VoxelMesh->SetCullDistance(10000);
}

// Called when the game starts or when spawned
void AVoxelBlock::BeginPlay()
{
    Super::BeginPlay();
}

// Called every frame
void AVoxelBlock::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

void AVoxelBlock::UpdateVoxelFace(int32 face)
{
    // Array to store face vertices
    TArray<FVector> FaceVertices;
    // Array to store face triangles (indices)
    TArray<int32> FaceTriangles;

    // Set up vertex positions for a cube (using the provided VoxelSize)
    FVector Vertices[8] = {
        FVector(-VoxelSize, -VoxelSize, -VoxelSize),  // 0: Bottom-left-back
        FVector(VoxelSize, -VoxelSize, -VoxelSize),   // 1: Bottom-right-back
        FVector(VoxelSize, VoxelSize, -VoxelSize),    // 2: Bottom-right-front
        FVector(-VoxelSize, VoxelSize, -VoxelSize),   // 3: Bottom-left-front
        FVector(-VoxelSize, -VoxelSize, VoxelSize),   // 4: Top-left-back
        FVector(VoxelSize, -VoxelSize, VoxelSize),    // 5: Top-right-back
        FVector(VoxelSize, VoxelSize, VoxelSize),     // 6: Top-right-front
        FVector(-VoxelSize, VoxelSize, VoxelSize)     // 7: Top-left-front
    };

    // Depending on the face, we set the correct vertices and normals
    switch (face)
    {
    case 0: // Top face
        FaceVertices = { Vertices[7], Vertices[6], Vertices[5], Vertices[4] };
        FaceTriangles = { 0, 1, 2, 0, 2, 3 };
        break;

    case 1: // Bottom face
        FaceVertices = { Vertices[0], Vertices[1], Vertices[2], Vertices[3] };
        FaceTriangles = { 0, 1, 2, 0, 2, 3 };
        break;

    case 2: // Left face
        FaceVertices = { Vertices[3], Vertices[7], Vertices[4], Vertices[0] };
        FaceTriangles = { 0, 1, 2, 0, 2, 3 };
        break;

    case 3: // Right face
        FaceVertices = { Vertices[5], Vertices[6], Vertices[2], Vertices[1] };
        FaceTriangles = { 0, 1, 2, 0, 2, 3 };
        break;

    case 4: // Front face
        FaceVertices = { Vertices[6], Vertices[7], Vertices[3], Vertices[2] };
        FaceTriangles = { 0, 1, 2, 0, 2, 3 };
        break;

    case 5: // Back face
        FaceVertices = { Vertices[4], Vertices[5], Vertices[1], Vertices[0] };
        FaceTriangles = { 0, 1, 2, 0, 2, 3 };
        break;
    }

    // Create a mesh section for the face
    VoxelMesh->CreateMeshSection(face, FaceVertices, FaceTriangles, Normals, UV0, VertexColors, Tangents, true);
}

