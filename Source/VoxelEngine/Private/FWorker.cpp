// Fill out your copyright notice in the Description page of Project Settings.


#include "FWorker.h"

FWorker::FWorker(FString& TaskName)
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