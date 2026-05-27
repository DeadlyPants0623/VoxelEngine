// Fill out your copyright notice in the Description page of Project Settings.

#include "SandboxGameMode.h"
#include "FlyCameraPawn.h"

ASandboxGameMode::ASandboxGameMode()
{
	DefaultPawnClass = AFlyCameraPawn::StaticClass();
}
