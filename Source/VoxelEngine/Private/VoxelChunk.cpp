#include "VoxelChunk.h"

#include "Math/UnrealMathUtility.h"

namespace VoxelTerrainSampling
{
FVector2D GetSeedNoiseOffset(int32 WorldSeed)
{
	return FVector2D(
		static_cast<float>(WorldSeed) * 131.0f,
		static_cast<float>(WorldSeed) * 193.0f + 4096.0f);
}

float SamplePerlin01(const FVector2D& Position, float Frequency)
{
	const float SafeFrequency = FMath::Max(Frequency, KINDA_SMALL_NUMBER);
	const float NoiseValue = FMath::PerlinNoise2D(Position * SafeFrequency);
	return FMath::Clamp((NoiseValue + 1.0f) * 0.5f, 0.0f, 1.0f);
}

FVoxelTerrainShapeParams MakeTerrainShapeParams(const FVoxelTerrainSettings& TerrainSettings)
{
	FVoxelTerrainShapeParams Params;
	Params.NoiseFrequency = TerrainSettings.NoiseFrequency;
	Params.HeightAmplitude = TerrainSettings.HeightAmplitude;
	Params.MountainStrength = TerrainSettings.MountainStrength;
	return Params;
}

float SampleFractalNoise01(
	const FVector2D& Position,
	const FVoxelTerrainSettings& TerrainSettings,
	float NoiseFrequency)
{
	const int32 OctaveCount = FMath::Max(1, TerrainSettings.Octaves);
	const float Persistence = FMath::Clamp(TerrainSettings.Persistence, 0.0f, 1.0f);
	const float Lacunarity = FMath::Max(TerrainSettings.Lacunarity, 1.0f);

	float Frequency = FMath::Max(NoiseFrequency, KINDA_SMALL_NUMBER);
	float Amplitude = 1.0f;
	float TotalNoise = 0.0f;
	float AmplitudeSum = 0.0f;

	for (int32 OctaveIndex = 0; OctaveIndex < OctaveCount; ++OctaveIndex)
	{
		TotalNoise += FMath::PerlinNoise2D(Position * Frequency) * Amplitude;
		AmplitudeSum += Amplitude;
		Frequency *= Lacunarity;
		Amplitude *= Persistence;
	}

	if (AmplitudeSum <= KINDA_SMALL_NUMBER)
	{
		return 0.5f;
	}

	return FMath::Clamp((TotalNoise / AmplitudeSum + 1.0f) * 0.5f, 0.0f, 1.0f);
}

float SampleRidgedNoise01(
	const FVector2D& Position,
	const FVoxelTerrainSettings& TerrainSettings,
	float NoiseFrequency)
{
	const int32 OctaveCount = FMath::Max(1, TerrainSettings.Octaves);
	const float Persistence = FMath::Clamp(TerrainSettings.Persistence, 0.0f, 1.0f);
	const float Lacunarity = FMath::Max(TerrainSettings.Lacunarity, 1.0f);
	const float RidgePower = FMath::Clamp(TerrainSettings.RidgedSharpnessExponent, 1.0f, 3.0f);

	const float FrequencyMultiplier = 2.25f;
	float Frequency = FMath::Max(NoiseFrequency * FrequencyMultiplier, KINDA_SMALL_NUMBER);
	float Amplitude = 1.0f;
	float TotalNoise = 0.0f;
	float AmplitudeSum = 0.0f;

	for (int32 OctaveIndex = 0; OctaveIndex < OctaveCount; ++OctaveIndex)
	{
		const float NoiseValue = FMath::PerlinNoise2D(Position * Frequency);
		const float RidgedNoise = 1.0f - FMath::Abs(NoiseValue);
		const float ShapedRidge = FMath::Pow(FMath::Max(RidgedNoise, 0.0f), RidgePower);
		TotalNoise += ShapedRidge * Amplitude;
		AmplitudeSum += Amplitude;
		Frequency *= Lacunarity;
		Amplitude *= Persistence;
	}

	if (AmplitudeSum <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	return FMath::Clamp(TotalNoise / AmplitudeSum, 0.0f, 1.0f);
}

struct FTerrainHeightProbe
{
	FVoxelTerrainShapeParams Shape;
	float Base01 = 0.0f;
	float Ridge01 = 0.0f;
	float Mask01 = 0.0f;
	float RidgeTermUnscaled = 0.0f;
	float RidgeTermFinal = 0.0f;
	float Terrain01 = 0.0f;
	float RawHeight = 0.0f;
};

float ComputeTerrainHeightInternal(
	const FVector2D& Position,
	const FVoxelTerrainSettings& TerrainSettings,
	FTerrainHeightProbe* OutProbe)
{
	const FVoxelTerrainShapeParams Shape = MakeTerrainShapeParams(TerrainSettings);
	const float BaseTerrain01 = SampleFractalNoise01(Position, TerrainSettings, Shape.NoiseFrequency);
	const float MountainMaskFrequency = FMath::Max(Shape.NoiseFrequency * 0.28f, KINDA_SMALL_NUMBER);
	const float MaskExponent = FMath::Clamp(TerrainSettings.MountainMaskExponent, 1.0f, 3.0f);
	const float MaskRaw = SamplePerlin01(Position + FVector2D(811.0f, 233.0f), MountainMaskFrequency);
	const float MountainMask01 = FMath::Pow(MaskRaw, MaskExponent);
	const float MountainRidge01 = SampleRidgedNoise01(
		Position + FVector2D(1777.0f, 911.0f),
		TerrainSettings,
		Shape.NoiseFrequency);
	const float RidgeTerm = MountainRidge01 * MountainMask01 * Shape.MountainStrength;
	const float Terrain01 = FMath::Clamp(BaseTerrain01 + RidgeTerm, 0.0f, 1.0f);
	const float ElevationFloor = FMath::Max(TerrainSettings.BaseHeight, TerrainSettings.SeaLevel);
	const float FinalHeight = ElevationFloor + (Terrain01 * Shape.HeightAmplitude);

	if (OutProbe != nullptr)
	{
		OutProbe->Shape = Shape;
		OutProbe->Base01 = BaseTerrain01;
		OutProbe->Ridge01 = MountainRidge01;
		OutProbe->Mask01 = MountainMask01;
		OutProbe->RidgeTermUnscaled = RidgeTerm;
		OutProbe->RidgeTermFinal = RidgeTerm;
		OutProbe->Terrain01 = Terrain01;
		OutProbe->RawHeight = FinalHeight;
	}

	return FinalHeight;
}

float SampleTerrainHeight(const FVector2D& Position, const FVoxelTerrainSettings& TerrainSettings)
{
	return ComputeTerrainHeightInternal(Position, TerrainSettings, nullptr);
}
} // namespace VoxelTerrainSampling

namespace
{
enum class EVoxelFaceDirection : uint8
{
	PositiveX,
	NegativeX,
	PositiveY,
	NegativeY,
	PositiveZ,
	NegativeZ
};

void GetFaceVoxelCoordinates(
	EVoxelFaceDirection Direction,
	int32 Slice,
	int32 U,
	int32 V,
	int32& OutX,
	int32& OutY,
	int32& OutZ)
{
	switch (Direction)
	{
	case EVoxelFaceDirection::PositiveX:
	case EVoxelFaceDirection::NegativeX:
		OutX = Slice;
		OutY = U;
		OutZ = V;
		break;

	case EVoxelFaceDirection::PositiveY:
	case EVoxelFaceDirection::NegativeY:
		OutX = U;
		OutY = Slice;
		OutZ = V;
		break;

	case EVoxelFaceDirection::PositiveZ:
	case EVoxelFaceDirection::NegativeZ:
	default:
		OutX = U;
		OutY = V;
		OutZ = Slice;
		break;
	}
}

static_assert(sizeof(AVoxelChunk::FVoxel) <= 4, "FVoxel should stay compact for heightfield caching.");

int32 GetColumnIndex(int32 X, int32 Y, int32 GridSize)
{
	return X + (Y * GridSize);
}

int32 GetVoxelIndex(int32 X, int32 Y, int32 Z, int32 GridSize)
{
	return X + (Y * GridSize) + (Z * GridSize * GridSize);
}

int32 GetBorderIndex(int32 AxisIndex, int32 Z, int32 GridSize, int32 ChunkHeight)
{
	if (AxisIndex < 0 || AxisIndex >= GridSize || Z < 0 || Z >= ChunkHeight)
	{
		return INDEX_NONE;
	}

	return AxisIndex + (Z * GridSize);
}

bool IsWithinColumnBounds(int32 X, int32 Y, int32 GridSize)
{
	return X >= 0 && X < GridSize
		&& Y >= 0 && Y < GridSize;
}

const AVoxelChunk::FVoxel* TryGetColumn(const TArray<AVoxelChunk::FVoxel>& Voxels, int32 GridSize, int32 X, int32 Y)
{
	if (!IsWithinColumnBounds(X, Y, GridSize))
	{
		return nullptr;
	}

	const int32 ColumnIndex = GetColumnIndex(X, Y, GridSize);
	return Voxels.IsValidIndex(ColumnIndex) ? &Voxels[ColumnIndex] : nullptr;
}

AVoxelChunk::EBlockType GetColumnBlockType(const TArray<AVoxelChunk::FVoxel>& Voxels, int32 GridSize, int32 X, int32 Y)
{
	if (const AVoxelChunk::FVoxel* Voxel = TryGetColumn(Voxels, GridSize, X, Y))
	{
		return Voxel->BlockType;
	}

	return AVoxelChunk::Air;
}

int32 GetColumnHeight(const TArray<AVoxelChunk::FVoxel>& Voxels, int32 GridSize, int32 X, int32 Y)
{
	if (const AVoxelChunk::FVoxel* Voxel = TryGetColumn(Voxels, GridSize, X, Y))
	{
		return Voxel->BlockType != AVoxelChunk::Air ? static_cast<int32>(Voxel->Height) : -1;
	}

	return -1;
}

AVoxelChunk::EBlockType GetBlockType(const TArray<AVoxelChunk::FVoxel>& Voxels, int32 GridSize, int32 ChunkHeight, int32 X, int32 Y, int32 Z)
{
	if (Z < 0 || Z >= ChunkHeight)
	{
		return AVoxelChunk::Air;
	}

	const AVoxelChunk::EBlockType ColumnBlockType = GetColumnBlockType(Voxels, GridSize, X, Y);
	return (ColumnBlockType != AVoxelChunk::Air && Z <= GetColumnHeight(Voxels, GridSize, X, Y))
		? ColumnBlockType
		: AVoxelChunk::Air;
}

bool IsLocalVoxelSolid(const TArray<AVoxelChunk::FVoxel>& Voxels, int32 GridSize, int32 ChunkHeight, int32 X, int32 Y, int32 Z)
{
	return GetBlockType(Voxels, GridSize, ChunkHeight, X, Y, Z) != AVoxelChunk::Air;
}

bool IsNeighborSolid(
	const FVoxelChunkNeighborBorderData& NeighborBorderData,
	EVoxelFaceDirection Direction,
	int32 X,
	int32 Y,
	int32 Z)
{
	switch (Direction)
	{
	case EVoxelFaceDirection::PositiveX:
		return NeighborBorderData.PositiveX.IsSet() && NeighborBorderData.PositiveX->IsNegativeXSolid(Y, Z);

	case EVoxelFaceDirection::NegativeX:
		return NeighborBorderData.NegativeX.IsSet() && NeighborBorderData.NegativeX->IsPositiveXSolid(Y, Z);

	case EVoxelFaceDirection::PositiveY:
		return NeighborBorderData.PositiveY.IsSet() && NeighborBorderData.PositiveY->IsNegativeYSolid(X, Z);

	case EVoxelFaceDirection::NegativeY:
		return NeighborBorderData.NegativeY.IsSet() && NeighborBorderData.NegativeY->IsPositiveYSolid(X, Z);

	case EVoxelFaceDirection::PositiveZ:
	case EVoxelFaceDirection::NegativeZ:
	default:
		return false;
	}
}

bool IsAdjacentSolidForFace(
	const TArray<AVoxelChunk::FVoxel>& Voxels,
	int32 GridSize,
	int32 ChunkHeight,
	const FVoxelChunkNeighborBorderData& NeighborBorderData,
	EVoxelFaceDirection Direction,
	int32 X,
	int32 Y,
	int32 Z)
{
	switch (Direction)
	{
	case EVoxelFaceDirection::PositiveX:
		return (X + 1 < GridSize)
			? IsLocalVoxelSolid(Voxels, GridSize, ChunkHeight, X + 1, Y, Z)
			: IsNeighborSolid(NeighborBorderData, Direction, X, Y, Z);

	case EVoxelFaceDirection::NegativeX:
		return (X - 1 >= 0)
			? IsLocalVoxelSolid(Voxels, GridSize, ChunkHeight, X - 1, Y, Z)
			: IsNeighborSolid(NeighborBorderData, Direction, X, Y, Z);

	case EVoxelFaceDirection::PositiveY:
		return (Y + 1 < GridSize)
			? IsLocalVoxelSolid(Voxels, GridSize, ChunkHeight, X, Y + 1, Z)
			: IsNeighborSolid(NeighborBorderData, Direction, X, Y, Z);

	case EVoxelFaceDirection::NegativeY:
		return (Y - 1 >= 0)
			? IsLocalVoxelSolid(Voxels, GridSize, ChunkHeight, X, Y - 1, Z)
			: IsNeighborSolid(NeighborBorderData, Direction, X, Y, Z);

	case EVoxelFaceDirection::PositiveZ:
		return (Z + 1 < ChunkHeight) && IsLocalVoxelSolid(Voxels, GridSize, ChunkHeight, X, Y, Z + 1);

	case EVoxelFaceDirection::NegativeZ:
		return (Z - 1 >= 0) && IsLocalVoxelSolid(Voxels, GridSize, ChunkHeight, X, Y, Z - 1);

	default:
		return false;
	}
}

FVector GridToLocalVertex(int32 GridX, int32 GridY, int32 GridZ, float VoxelSize)
{
	const float CellSize = VoxelSize * 2.0f;
	const FVector OriginOffset(-VoxelSize, -VoxelSize, -VoxelSize);
	return FVector(GridX * CellSize, GridY * CellSize, GridZ * CellSize) + OriginOffset;
}

int32 GetEstimatedFaceBudget(int32 GridSize, int32 ChunkHeight)
{
	const int32 TopAndBottomFaces = GridSize * GridSize * 2;
	const int32 SideFaces = GridSize * ChunkHeight * 4;
	return FMath::Max(256, TopAndBottomFaces + SideFaces);
}

void ReserveMeshBuffers(FVoxelChunkMeshData& OutMeshData, int32 EstimatedFaceCount)
{
	OutMeshData.Vertices.Reserve(EstimatedFaceCount * 4);
	OutMeshData.Triangles.Reserve(EstimatedFaceCount * 6);
	OutMeshData.Normals.Reserve(EstimatedFaceCount * 4);
	OutMeshData.UVs.Reserve(EstimatedFaceCount * 4);
	OutMeshData.VertexColors.Reserve(EstimatedFaceCount * 4);
	OutMeshData.Tangents.Reserve(EstimatedFaceCount * 4);
}

FVector GetFaceNormal(EVoxelFaceDirection Direction)
{
	switch (Direction)
	{
	case EVoxelFaceDirection::PositiveX:
		return FVector(1.0f, 0.0f, 0.0f);

	case EVoxelFaceDirection::NegativeX:
		return FVector(-1.0f, 0.0f, 0.0f);

	case EVoxelFaceDirection::PositiveY:
		return FVector(0.0f, 1.0f, 0.0f);

	case EVoxelFaceDirection::NegativeY:
		return FVector(0.0f, -1.0f, 0.0f);

	case EVoxelFaceDirection::PositiveZ:
		return FVector::UpVector;

	case EVoxelFaceDirection::NegativeZ:
	default:
		return -FVector::UpVector;
	}
}

FProcMeshTangent GetFaceTangent(EVoxelFaceDirection Direction)
{
	switch (Direction)
	{
	case EVoxelFaceDirection::PositiveX:
	case EVoxelFaceDirection::NegativeX:
		return FProcMeshTangent(0.0f, 1.0f, 0.0f);

	case EVoxelFaceDirection::PositiveY:
	case EVoxelFaceDirection::NegativeY:
		return FProcMeshTangent(0.0f, 0.0f, 1.0f);

	case EVoxelFaceDirection::PositiveZ:
	case EVoxelFaceDirection::NegativeZ:
	default:
		return FProcMeshTangent(1.0f, 0.0f, 0.0f);
	}
}

void AppendQuad(
	FVoxelChunkMeshData& OutMeshData,
	const FVector& Vertex0,
	const FVector& Vertex1,
	const FVector& Vertex2,
	const FVector& Vertex3,
	const FVector& Normal,
	const FProcMeshTangent& Tangent,
	const FVector2D& UV0,
	const FVector2D& UV1,
	const FVector2D& UV2,
	const FVector2D& UV3)
{
	const int32 BaseIndex = OutMeshData.Vertices.Num();
	OutMeshData.Vertices.AddUninitialized(4);
	OutMeshData.Vertices[BaseIndex + 0] = Vertex0;
	OutMeshData.Vertices[BaseIndex + 1] = Vertex1;
	OutMeshData.Vertices[BaseIndex + 2] = Vertex2;
	OutMeshData.Vertices[BaseIndex + 3] = Vertex3;

	const int32 TriangleBaseIndex = OutMeshData.Triangles.Num();
	OutMeshData.Triangles.AddUninitialized(6);
	OutMeshData.Triangles[TriangleBaseIndex + 0] = BaseIndex + 0;
	OutMeshData.Triangles[TriangleBaseIndex + 1] = BaseIndex + 1;
	OutMeshData.Triangles[TriangleBaseIndex + 2] = BaseIndex + 2;
	OutMeshData.Triangles[TriangleBaseIndex + 3] = BaseIndex + 0;
	OutMeshData.Triangles[TriangleBaseIndex + 4] = BaseIndex + 2;
	OutMeshData.Triangles[TriangleBaseIndex + 5] = BaseIndex + 3;

	const int32 AttributeBaseIndex = OutMeshData.Normals.Num();
	OutMeshData.Normals.AddUninitialized(4);
	OutMeshData.UVs.AddUninitialized(4);
	OutMeshData.VertexColors.AddUninitialized(4);
	OutMeshData.Tangents.AddUninitialized(4);

	OutMeshData.Normals[AttributeBaseIndex + 0] = Normal;
	OutMeshData.Normals[AttributeBaseIndex + 1] = Normal;
	OutMeshData.Normals[AttributeBaseIndex + 2] = Normal;
	OutMeshData.Normals[AttributeBaseIndex + 3] = Normal;

	OutMeshData.UVs[AttributeBaseIndex + 0] = UV0;
	OutMeshData.UVs[AttributeBaseIndex + 1] = UV1;
	OutMeshData.UVs[AttributeBaseIndex + 2] = UV2;
	OutMeshData.UVs[AttributeBaseIndex + 3] = UV3;

	OutMeshData.VertexColors[AttributeBaseIndex + 0] = FColor::White;
	OutMeshData.VertexColors[AttributeBaseIndex + 1] = FColor::White;
	OutMeshData.VertexColors[AttributeBaseIndex + 2] = FColor::White;
	OutMeshData.VertexColors[AttributeBaseIndex + 3] = FColor::White;

	OutMeshData.Tangents[AttributeBaseIndex + 0] = Tangent;
	OutMeshData.Tangents[AttributeBaseIndex + 1] = Tangent;
	OutMeshData.Tangents[AttributeBaseIndex + 2] = Tangent;
	OutMeshData.Tangents[AttributeBaseIndex + 3] = Tangent;
}

void AppendQuad(
	FVoxelChunkMeshData& OutMeshData,
	const FVector& Vertex0,
	const FVector& Vertex1,
	const FVector& Vertex2,
	const FVector& Vertex3,
	const FVector& Normal,
	const FProcMeshTangent& Tangent,
	float UExtent,
	float VExtent)
{
	// Keep UVs in voxel-tile units so greedy side faces repeat instead of stretching.
	AppendQuad(
		OutMeshData,
		Vertex0,
		Vertex1,
		Vertex2,
		Vertex3,
		Normal,
		Tangent,
		FVector2D(0.0f, 0.0f),
		FVector2D(UExtent, 0.0f),
		FVector2D(UExtent, VExtent),
		FVector2D(0.0f, VExtent));
}

void AppendGreedyQuad(
	FVoxelChunkMeshData& OutMeshData,
	EVoxelFaceDirection Direction,
	int32 Slice,
	int32 UStart,
	int32 VStart,
	int32 USize,
	int32 VSize,
	float VoxelSize)
{
	const int32 UEnd = UStart + USize;
	const int32 VEnd = VStart + VSize;

	switch (Direction)
	{
	case EVoxelFaceDirection::PositiveX:
	{
		const int32 XPlane = Slice + 1;
		AppendQuad(
			OutMeshData,
			GridToLocalVertex(XPlane, UStart, VEnd, VoxelSize),
			GridToLocalVertex(XPlane, UEnd, VEnd, VoxelSize),
			GridToLocalVertex(XPlane, UEnd, VStart, VoxelSize),
			GridToLocalVertex(XPlane, UStart, VStart, VoxelSize),
			GetFaceNormal(Direction),
			GetFaceTangent(Direction),
			static_cast<float>(USize),
			static_cast<float>(VSize));
		break;
	}

	case EVoxelFaceDirection::NegativeX:
	{
		const int32 XPlane = Slice;
		AppendQuad(
			OutMeshData,
			GridToLocalVertex(XPlane, UEnd, VStart, VoxelSize),
			GridToLocalVertex(XPlane, UEnd, VEnd, VoxelSize),
			GridToLocalVertex(XPlane, UStart, VEnd, VoxelSize),
			GridToLocalVertex(XPlane, UStart, VStart, VoxelSize),
			GetFaceNormal(Direction),
			GetFaceTangent(Direction),
			FVector2D(static_cast<float>(USize), 0.0f),
			FVector2D(static_cast<float>(USize), static_cast<float>(VSize)),
			FVector2D(0.0f, static_cast<float>(VSize)),
			FVector2D(0.0f, 0.0f));
		break;
	}

	case EVoxelFaceDirection::PositiveY:
	{
		const int32 YPlane = Slice + 1;
		AppendQuad(
			OutMeshData,
			GridToLocalVertex(UEnd, YPlane, VEnd, VoxelSize),
			GridToLocalVertex(UStart, YPlane, VEnd, VoxelSize),
			GridToLocalVertex(UStart, YPlane, VStart, VoxelSize),
			GridToLocalVertex(UEnd, YPlane, VStart, VoxelSize),
			GetFaceNormal(Direction),
			GetFaceTangent(Direction),
			static_cast<float>(USize),
			static_cast<float>(VSize));
		break;
	}

	case EVoxelFaceDirection::NegativeY:
	{
		const int32 YPlane = Slice;
		AppendQuad(
			OutMeshData,
			GridToLocalVertex(UStart, YPlane, VEnd, VoxelSize),
			GridToLocalVertex(UEnd, YPlane, VEnd, VoxelSize),
			GridToLocalVertex(UEnd, YPlane, VStart, VoxelSize),
			GridToLocalVertex(UStart, YPlane, VStart, VoxelSize),
			GetFaceNormal(Direction),
			GetFaceTangent(Direction),
			static_cast<float>(USize),
			static_cast<float>(VSize));
		break;
	}

	case EVoxelFaceDirection::PositiveZ:
	{
		const int32 ZPlane = Slice + 1;
		AppendQuad(
			OutMeshData,
			GridToLocalVertex(UStart, VEnd, ZPlane, VoxelSize),
			GridToLocalVertex(UEnd, VEnd, ZPlane, VoxelSize),
			GridToLocalVertex(UEnd, VStart, ZPlane, VoxelSize),
			GridToLocalVertex(UStart, VStart, ZPlane, VoxelSize),
			GetFaceNormal(Direction),
			GetFaceTangent(Direction),
			static_cast<float>(USize),
			static_cast<float>(VSize));
		break;
	}

	case EVoxelFaceDirection::NegativeZ:
	default:
	{
		const int32 ZPlane = Slice;
		AppendQuad(
			OutMeshData,
			GridToLocalVertex(UStart, VStart, ZPlane, VoxelSize),
			GridToLocalVertex(UEnd, VStart, ZPlane, VoxelSize),
			GridToLocalVertex(UEnd, VEnd, ZPlane, VoxelSize),
			GridToLocalVertex(UStart, VEnd, ZPlane, VoxelSize),
			GetFaceNormal(Direction),
			GetFaceTangent(Direction),
			static_cast<float>(USize),
			static_cast<float>(VSize));
		break;
	}
	}
}

void BuildGreedyFacesForDirection(
	FVoxelChunkMeshData& OutMeshData,
	const TArray<AVoxelChunk::FVoxel>& Voxels,
	int32 GridSize,
	int32 ChunkHeight,
	float VoxelSize,
	const FVoxelChunkNeighborBorderData& NeighborBorderData,
	EVoxelFaceDirection Direction,
	TArray<uint8>& SurfaceVoxelFlags)
{
	int32 SliceCount = 0;
	int32 UDim = 0;
	int32 VDim = 0;

	switch (Direction)
	{
	case EVoxelFaceDirection::PositiveX:
	case EVoxelFaceDirection::NegativeX:
		SliceCount = GridSize;
		UDim = GridSize;
		VDim = ChunkHeight;
		break;

	case EVoxelFaceDirection::PositiveY:
	case EVoxelFaceDirection::NegativeY:
		SliceCount = GridSize;
		UDim = GridSize;
		VDim = ChunkHeight;
		break;

	case EVoxelFaceDirection::PositiveZ:
	case EVoxelFaceDirection::NegativeZ:
	default:
		SliceCount = ChunkHeight;
		UDim = GridSize;
		VDim = GridSize;
		break;
	}

	TArray<AVoxelChunk::EBlockType> Mask;
	Mask.SetNumUninitialized(SliceCount > 0 ? UDim * VDim : 0);

	for (int32 Slice = 0; Slice < SliceCount; ++Slice)
	{
		for (int32 V = 0; V < VDim; ++V)
		{
			for (int32 U = 0; U < UDim; ++U)
			{
				const int32 MaskIndex = U + (V * UDim);
				int32 X = 0;
				int32 Y = 0;
				int32 Z = 0;
				GetFaceVoxelCoordinates(Direction, Slice, U, V, X, Y, Z);

				AVoxelChunk::EBlockType BlockType = GetBlockType(Voxels, GridSize, ChunkHeight, X, Y, Z);
				if (BlockType != AVoxelChunk::Air
					&& IsAdjacentSolidForFace(Voxels, GridSize, ChunkHeight, NeighborBorderData, Direction, X, Y, Z))
				{
					BlockType = AVoxelChunk::Air;
				}

				Mask[MaskIndex] = BlockType;
				if (BlockType != AVoxelChunk::Air)
				{
					SurfaceVoxelFlags[GetVoxelIndex(X, Y, Z, GridSize)] = 1;
				}
			}
		}

		for (int32 V = 0; V < VDim; ++V)
		{
			for (int32 U = 0; U < UDim;)
			{
				const int32 MaskIndex = U + (V * UDim);
				const AVoxelChunk::EBlockType BlockType = Mask[MaskIndex];
				if (BlockType == AVoxelChunk::Air)
				{
					++U;
					continue;
				}

				int32 Width = 1;
				while (U + Width < UDim && Mask[MaskIndex + Width] == BlockType)
				{
					++Width;
				}

				int32 Height = 1;
				bool bCanGrow = true;
				while (V + Height < VDim && bCanGrow)
				{
					for (int32 WidthOffset = 0; WidthOffset < Width; ++WidthOffset)
					{
						if (Mask[(U + WidthOffset) + ((V + Height) * UDim)] != BlockType)
						{
							bCanGrow = false;
							break;
						}
					}

					if (bCanGrow)
					{
						++Height;
					}
				}

				for (int32 HeightOffset = 0; HeightOffset < Height; ++HeightOffset)
				{
					for (int32 WidthOffset = 0; WidthOffset < Width; ++WidthOffset)
					{
						Mask[(U + WidthOffset) + ((V + HeightOffset) * UDim)] = AVoxelChunk::Air;
					}
				}

				AppendGreedyQuad(OutMeshData, Direction, Slice, U, V, Width, Height, VoxelSize);
				U += Width;
			}
		}
	}
}

void GenerateVoxelRange(
	TArray<AVoxelChunk::FVoxel>& Voxels,
	FVoxelTerrainSampleStats& OutTerrainStats,
	int32 GridSize,
	int32 ChunkHeight,
	const FVector2D& ChunkOffset,
	const FVoxelTerrainSettings& TerrainSettings,
	int32 WorldSeed,
	int32 StartX,
	int32 EndX)
{
	const FVector2D SeedNoiseOffset = VoxelTerrainSampling::GetSeedNoiseOffset(WorldSeed);

	for (int32 X = StartX; X < EndX; ++X)
	{
		for (int32 Y = 0; Y < GridSize; ++Y)
		{
			const float GlobalX = SeedNoiseOffset.X + ChunkOffset.X + X;
			const float GlobalY = SeedNoiseOffset.Y + ChunkOffset.Y + Y;
			const float SampledHeight = VoxelTerrainSampling::SampleTerrainHeight(FVector2D(GlobalX, GlobalY), TerrainSettings);
			const int32 Height = FMath::Clamp(FMath::RoundToInt(SampledHeight), 0, ChunkHeight - 1);
			OutTerrainStats.Accumulate(Height);
			const int32 ColumnIndex = GetColumnIndex(X, Y, GridSize);
			AVoxelChunk::FVoxel& Voxel = Voxels[ColumnIndex];
			Voxel.Height = static_cast<uint16>(Height);
			Voxel.BlockType = AVoxelChunk::Dirt;
		}
	}
}
}

void FVoxelChunkBorderData::Reset(int32 InGridSize, int32 InChunkHeight)
{
	GridSize = InGridSize;
	ChunkHeight = InChunkHeight;

	const int32 StripSize = GridSize * ChunkHeight;
	PositiveX.Init(0, StripSize);
	NegativeX.Init(0, StripSize);
	PositiveY.Init(0, StripSize);
	NegativeY.Init(0, StripSize);
}

bool FVoxelChunkBorderData::HasData() const
{
	const int32 ExpectedStripSize = GridSize * ChunkHeight;
	return GridSize > 0
		&& ChunkHeight > 0
		&& PositiveX.Num() == ExpectedStripSize
		&& NegativeX.Num() == ExpectedStripSize
		&& PositiveY.Num() == ExpectedStripSize
		&& NegativeY.Num() == ExpectedStripSize;
}

bool FVoxelChunkBorderData::IsPositiveXSolid(int32 Y, int32 Z) const
{
	const int32 Index = GetBorderIndex(Y, Z, GridSize, ChunkHeight);
	return PositiveX.IsValidIndex(Index) && PositiveX[Index] != 0;
}

bool FVoxelChunkBorderData::IsNegativeXSolid(int32 Y, int32 Z) const
{
	const int32 Index = GetBorderIndex(Y, Z, GridSize, ChunkHeight);
	return NegativeX.IsValidIndex(Index) && NegativeX[Index] != 0;
}

bool FVoxelChunkBorderData::IsPositiveYSolid(int32 X, int32 Z) const
{
	const int32 Index = GetBorderIndex(X, Z, GridSize, ChunkHeight);
	return PositiveY.IsValidIndex(Index) && PositiveY[Index] != 0;
}

bool FVoxelChunkBorderData::IsNegativeYSolid(int32 X, int32 Z) const
{
	const int32 Index = GetBorderIndex(X, Z, GridSize, ChunkHeight);
	return NegativeY.IsValidIndex(Index) && NegativeY[Index] != 0;
}

void FVoxelChunkMeshData::Reset()
{
	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	UVs.Reset();
	VertexColors.Reset();
	Tangents.Reset();
	Stats = FVoxelChunkMeshStats{};
}

bool FVoxelChunkMeshData::HasGeometry() const
{
	return Vertices.Num() > 0 && Triangles.Num() > 0;
}

AVoxelChunk::AVoxelChunk()
	: GridSize(0)
	, VoxelSize(0.0f)
	, ChunkHeight(0)
	, DirtMaterial(nullptr)
	, bCollisionEnabled(true)
{
	PrimaryActorTick.bCanEverTick = false;

	ChunkMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ChunkMesh"));
	RootComponent = ChunkMesh;
	ChunkMesh->bUseAsyncCooking = true;
	ChunkMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ChunkMesh->SetCollisionObjectType(ECC_WorldDynamic);
	ChunkMesh->SetCollisionResponseToAllChannels(ECR_Block);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DirtMaterialAsset(TEXT("/Game/Megascans/Surfaces/Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs/MI_Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs_8K.MI_Desert_Outback_Ground_Dirt_Rocky_09_xisbchvs_8K"));
	if (DirtMaterialAsset.Succeeded())
	{
		DirtMaterial = DirtMaterialAsset.Object;
	}
}

void AVoxelChunk::BeginPlay()
{
	Super::BeginPlay();
}

void AVoxelChunk::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AVoxelChunk::Initialize(int32 InGridSize, float InVoxelSize, FVector InChunkPosition, FVector2D InChunkOffset, int32 InChunkHeight)
{
	GridSize = InGridSize;
	VoxelSize = InVoxelSize;
	ChunkPosition = InChunkPosition;
	ChunkOffset = InChunkOffset;
	ChunkHeight = InChunkHeight;

	SetActorLocation(ChunkPosition);
	MeshStats = FVoxelChunkMeshStats{};
}

void AVoxelChunk::BuildVoxelData(
	TArray<FVoxel>& OutVoxels,
	FVoxelTerrainSampleStats& OutTerrainStats,
	FVoxelChunkBorderData& OutBorderData,
	int32 InGridSize,
	int32 InChunkHeight,
	const FVector2D& InChunkOffset,
	const FVoxelTerrainSettings& InTerrainSettings,
	int32 InWorldSeed)
{
	OutVoxels.SetNumUninitialized(InGridSize * InGridSize);
	OutTerrainStats = FVoxelTerrainSampleStats{};
	GenerateVoxelRange(OutVoxels, OutTerrainStats, InGridSize, InChunkHeight, InChunkOffset, InTerrainSettings, InWorldSeed, 0, InGridSize);
	BuildBorderData(OutBorderData, OutVoxels, InGridSize, InChunkHeight);
}

void AVoxelChunk::BuildBorderData(
	FVoxelChunkBorderData& OutBorderData,
	const TArray<FVoxel>& InVoxels,
	int32 InGridSize,
	int32 InChunkHeight)
{
	OutBorderData.Reset(InGridSize, InChunkHeight);

	for (int32 Y = 0; Y < InGridSize; ++Y)
	{
		const int32 PositiveXHeight = GetColumnHeight(InVoxels, InGridSize, InGridSize - 1, Y);
		const int32 NegativeXHeight = GetColumnHeight(InVoxels, InGridSize, 0, Y);
		for (int32 Z = 0; Z < InChunkHeight; ++Z)
		{
			const int32 BorderIndex = GetBorderIndex(Y, Z, InGridSize, InChunkHeight);
			OutBorderData.PositiveX[BorderIndex] = Z <= PositiveXHeight ? 1 : 0;
			OutBorderData.NegativeX[BorderIndex] = Z <= NegativeXHeight ? 1 : 0;
		}
	}

	for (int32 X = 0; X < InGridSize; ++X)
	{
		const int32 PositiveYHeight = GetColumnHeight(InVoxels, InGridSize, X, InGridSize - 1);
		const int32 NegativeYHeight = GetColumnHeight(InVoxels, InGridSize, X, 0);
		for (int32 Z = 0; Z < InChunkHeight; ++Z)
		{
			const int32 BorderIndex = GetBorderIndex(X, Z, InGridSize, InChunkHeight);
			OutBorderData.PositiveY[BorderIndex] = Z <= PositiveYHeight ? 1 : 0;
			OutBorderData.NegativeY[BorderIndex] = Z <= NegativeYHeight ? 1 : 0;
		}
	}
}

void AVoxelChunk::BuildMeshData(
	FVoxelChunkMeshData& OutMeshData,
	const TArray<FVoxel>& InVoxels,
	int32 InGridSize,
	int32 InChunkHeight,
	float InVoxelSize,
	const FVoxelChunkNeighborBorderData& NeighborBorderData)
{
	OutMeshData.Reset();
	ReserveMeshBuffers(OutMeshData, GetEstimatedFaceBudget(InGridSize, InChunkHeight));

	TArray<uint8> SurfaceVoxelFlags;
	SurfaceVoxelFlags.Init(0, InGridSize * InGridSize * InChunkHeight);

	BuildGreedyFacesForDirection(OutMeshData, InVoxels, InGridSize, InChunkHeight, InVoxelSize, NeighborBorderData, EVoxelFaceDirection::PositiveX, SurfaceVoxelFlags);
	BuildGreedyFacesForDirection(OutMeshData, InVoxels, InGridSize, InChunkHeight, InVoxelSize, NeighborBorderData, EVoxelFaceDirection::NegativeX, SurfaceVoxelFlags);
	BuildGreedyFacesForDirection(OutMeshData, InVoxels, InGridSize, InChunkHeight, InVoxelSize, NeighborBorderData, EVoxelFaceDirection::PositiveY, SurfaceVoxelFlags);
	BuildGreedyFacesForDirection(OutMeshData, InVoxels, InGridSize, InChunkHeight, InVoxelSize, NeighborBorderData, EVoxelFaceDirection::NegativeY, SurfaceVoxelFlags);
	BuildGreedyFacesForDirection(OutMeshData, InVoxels, InGridSize, InChunkHeight, InVoxelSize, NeighborBorderData, EVoxelFaceDirection::PositiveZ, SurfaceVoxelFlags);
	BuildGreedyFacesForDirection(OutMeshData, InVoxels, InGridSize, InChunkHeight, InVoxelSize, NeighborBorderData, EVoxelFaceDirection::NegativeZ, SurfaceVoxelFlags);

	int32 SolidVoxelCount = 0;
	int32 SurfaceVoxelCount = 0;
	for (int32 Y = 0; Y < InGridSize; ++Y)
	{
		for (int32 X = 0; X < InGridSize; ++X)
		{
			const FVoxel* Voxel = TryGetColumn(InVoxels, InGridSize, X, Y);
			if (Voxel == nullptr || Voxel->BlockType == Air)
			{
				continue;
			}

			const int32 ColumnHeight = static_cast<int32>(Voxel->Height);
			SolidVoxelCount += ColumnHeight + 1;
			for (int32 Z = 0; Z <= ColumnHeight; ++Z)
			{
				SurfaceVoxelCount += SurfaceVoxelFlags[GetVoxelIndex(X, Y, Z, InGridSize)] != 0 ? 1 : 0;
			}
		}
	}

	OutMeshData.Stats.SolidVoxelCount = SolidVoxelCount;
	OutMeshData.Stats.SurfaceVoxelCount = SurfaceVoxelCount;
	OutMeshData.Stats.CulledInteriorVoxelCount = SolidVoxelCount - SurfaceVoxelCount;
	OutMeshData.Stats.FaceCount = OutMeshData.Triangles.Num() / 6;
	OutMeshData.Stats.TriangleCount = OutMeshData.Triangles.Num() / 3;
	OutMeshData.Stats.VertexCount = OutMeshData.Vertices.Num();
}

void AVoxelChunk::CalculateVoxels(int32 StartX, int32 EndX, const FVoxelTerrainSettings& InTerrainSettings, int32 InWorldSeed)
{
	if (Voxels.Num() != GridSize * GridSize)
	{
		Voxels.SetNumUninitialized(GridSize * GridSize);
	}

	FVoxelTerrainSampleStats UnusedTerrainStats;
	GenerateVoxelRange(Voxels, UnusedTerrainStats, GridSize, ChunkHeight, ChunkOffset, InTerrainSettings, InWorldSeed, StartX, EndX);
}

void AVoxelChunk::SetVoxelData(TArray<FVoxel>&& InVoxels)
{
	Voxels = MoveTemp(InVoxels);
}

void AVoxelChunk::ApplyMeshData(const FVoxelChunkMeshData& InMeshData)
{
	if (InMeshData.HasGeometry())
	{
		ChunkMesh->CreateMeshSection(0, InMeshData.Vertices, InMeshData.Triangles, InMeshData.Normals, InMeshData.UVs, InMeshData.VertexColors, InMeshData.Tangents, bCollisionEnabled);

		if (DirtMaterial != nullptr)
		{
			ChunkMesh->SetMaterial(0, DirtMaterial);
		}
	}
	else
	{
		ChunkMesh->ClearMeshSection(0);
	}

	MeshStats = InMeshData.Stats;
}

void AVoxelChunk::SetChunkCollisionEnabled(bool bShouldEnableCollision, const FVoxelChunkMeshData* InMeshData)
{
	if (ChunkMesh == nullptr)
	{
		return;
	}

	const ECollisionEnabled::Type CollisionMode = bShouldEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
	if (bCollisionEnabled == bShouldEnableCollision)
	{
		ChunkMesh->SetCollisionEnabled(CollisionMode);
		return;
	}

	bCollisionEnabled = bShouldEnableCollision;
	ChunkMesh->SetCollisionEnabled(CollisionMode);
	if (InMeshData != nullptr && InMeshData->HasGeometry())
	{
		ChunkMesh->CreateMeshSection(0, InMeshData->Vertices, InMeshData->Triangles, InMeshData->Normals, InMeshData->UVs, InMeshData->VertexColors, InMeshData->Tangents, bCollisionEnabled);
		if (DirtMaterial != nullptr)
		{
			ChunkMesh->SetMaterial(0, DirtMaterial);
		}
	}
}

void AVoxelChunk::SetChunkRendered(bool bShouldRender)
{
	if (ChunkMesh == nullptr)
	{
		return;
	}

	ChunkMesh->SetVisibility(bShouldRender, true);
	ChunkMesh->SetHiddenInGame(!bShouldRender, true);
}

void AVoxelChunk::SpawnVoxelBlocks()
{
	FVoxelChunkMeshData MeshData;
	const FVoxelChunkNeighborBorderData EmptyNeighborBorderData;
	BuildMeshData(MeshData, Voxels, GridSize, ChunkHeight, VoxelSize, EmptyNeighborBorderData);
	ApplyMeshData(MeshData);
}

void AVoxelChunk::DestroyVoxels()
{
	Voxels.Empty();
	ChunkMesh->ClearAllMeshSections();
	MeshStats = FVoxelChunkMeshStats{};
}
