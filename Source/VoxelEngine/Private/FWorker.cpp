// Fill out your copyright notice in the Description page of Project Settings.


#include "FWorker.h"

FWorker::FWorker(FString& TaskName)
	: bRunThread(true)
{
	Thread = FRunnableThread::Create(this, *TaskName);
}

FWorker::~FWorker()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
}

bool FWorker::Init()
{
	return true;
}

uint32 FWorker::Run()
{
	// Extend this when the worker has real per-thread work; exit so the thread does not spin idle.
	return 0;
}

void FWorker::Stop()
{
	bRunThread = false;
}