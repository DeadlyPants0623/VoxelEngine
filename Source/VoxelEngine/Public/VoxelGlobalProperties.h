// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "VoxelGlobalProperties.generated.h"

/**
 * 
 */
UCLASS(Blueprintable)
class VOXELENGINE_API UVoxelGlobalProperties : public UObject
{
	GENERATED_BODY()
	
public:
	UVoxelGlobalProperties();

	//static UVoxelGlobalProperties* GetProperties();

	int32 VoxelSize;
	int32 VoxelGrid;
};
