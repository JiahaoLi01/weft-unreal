#include "Render/WeftOwnerThread.h"

#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

FWeftOwnerThread::FWeftOwnerThread()
{
	Ready = FPlatformProcess::GetSynchEventFromPool(false);
	Done = FPlatformProcess::GetSynchEventFromPool(false);
	Thread = FRunnableThread::Create(this, TEXT("WeftRender"), 0, TPri_AboveNormal);
}

FWeftOwnerThread::~FWeftOwnerThread()
{
	Stop();
	Ready->Trigger();
	Thread->WaitForCompletion();
	delete Thread;
	FPlatformProcess::ReturnSynchEventToPool(Ready);
	FPlatformProcess::ReturnSynchEventToPool(Done);
}

void FWeftOwnerThread::Call(TUniqueFunction<void()> Work)
{
	// One caller at a time: the RHI's lambdas run one after the other, and shutdown after them.
	FScopeLock Lock(&Calls);
	Pending = MoveTemp(Work);
	Ready->Trigger();
	Done->Wait();
}

uint32 FWeftOwnerThread::Run()
{
	while (true)
	{
		Ready->Wait();
		if (bStopping)
		{
			return 0;
		}
		Pending();
		Pending.Reset();
		Done->Trigger();
	}
}

void FWeftOwnerThread::Stop()
{
	bStopping = true;
}
