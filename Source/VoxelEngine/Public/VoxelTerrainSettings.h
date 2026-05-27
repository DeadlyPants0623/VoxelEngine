#pragma once

#include "CoreMinimal.h"
#include "VoxelTerrainSettings.generated.h"

USTRUCT(BlueprintType)
struct VOXELENGINE_API FVoxelTerrainSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "0.0"))
	float BaseHeight = 36.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "0.0"))
	float HeightAmplitude = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "0.0001"))
	float NoiseFrequency = 0.0155f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "1"))
	int32 Octaves = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Persistence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "1.0"))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "0.0"))
	float SeaLevel = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "0.0"))
	float MountainStrength = 0.42f;

	// Mountain mask = Perlin^this. Lower (e.g. 1.2–1.5) = broader highlands; 2+ = sparse needle peaks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float MountainMaskExponent = 1.32f;

	// Ridged layer = (1-|n|)^this. Lower = rounder ridges; 2 = very sharp spikes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float RidgedSharpnessExponent = 1.55f;
};

// Cached per-column shape knobs (mirrors FVoxelTerrainSettings heightfield fields).
struct FVoxelTerrainShapeParams
{
	float NoiseFrequency = 0.0155f;
	float HeightAmplitude = 90.0f;
	float MountainStrength = 0.42f;
};

struct FVoxelTerrainSampleStats
{
	int32 MinHeight = TNumericLimits<int32>::Max();
	int32 MaxHeight = TNumericLimits<int32>::Lowest();
	double TotalHeight = 0.0;
	int64 SampleCount = 0;

	void Accumulate(int32 Height)
	{
		MinHeight = FMath::Min(MinHeight, Height);
		MaxHeight = FMath::Max(MaxHeight, Height);
		TotalHeight += Height;
		++SampleCount;
	}

	void Merge(const FVoxelTerrainSampleStats& Other)
	{
		if (!Other.HasSamples())
		{
			return;
		}

		MinHeight = FMath::Min(MinHeight, Other.MinHeight);
		MaxHeight = FMath::Max(MaxHeight, Other.MaxHeight);
		TotalHeight += Other.TotalHeight;
		SampleCount += Other.SampleCount;
	}

	bool HasSamples() const
	{
		return SampleCount > 0;
	}

	double GetAverageHeight() const
	{
		return HasSamples() ? TotalHeight / static_cast<double>(SampleCount) : 0.0;
	}
};
