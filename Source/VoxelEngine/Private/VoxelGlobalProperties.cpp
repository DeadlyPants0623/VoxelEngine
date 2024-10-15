// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelGlobalProperties.h"

UVoxelGlobalProperties::UVoxelGlobalProperties()
{
	VoxelSize = 100;
	VoxelGrid = 50;
}

//UVoxelGlobalProperties* UVoxelGlobalProperties::GetProperties()
//{
//	UVoxelGlobalProperties* VoxelGlobalProperties = NewObject<UVoxelGlobalProperties>();
//
//	if (VoxelGlobalProperties == nullptr)
//	{
//		UE_LOG(LogTemp, Warning, TEXT("VoxelGlobalProperties : ADDED"));
//	}
//	else
//	{
//		UE_LOG(LogTemp, Warning, TEXT("VoxelGlobalProperties : NULL"));
//	}
//	return VoxelGlobalProperties;
//}