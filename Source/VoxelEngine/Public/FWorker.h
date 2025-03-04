// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class VOXELENGINE_API FWorker : public FRunnable
{
public:
	FWorker(FString& TaskName);
	virtual ~FWorker() override;

	// Overriden from FRunnable
	// Do not call these functions youself, that will happen automatically
	bool Init() override; // Do your setup here, allocate memory, ect.
	uint32 Run() override; // Main data processing happens here
	void Stop() override; // Clean up any memory you allocated here

protected:
	FRunnableThread* Thread = nullptr; // Thread for the worker
	bool bRunThread; // Flag to run the thread

};
