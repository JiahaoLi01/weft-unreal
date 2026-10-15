#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Templates/Function.h"

class FEvent;
class FRunnableThread;

/**
 * The owner thread of a weft renderer: every call into the renderer runs here, one at a time. The RHI executes its
 * command lists' lambdas on whatever thread it picks (Unreal 5's RHI runs them as tasks), while a weft renderer
 * answers only its creating thread; a lambda hands its work here and waits for it, so the work still lands in the
 * command list at the lambda's place.
 */
class FWeftOwnerThread final : public FRunnable
{
public:
	FWeftOwnerThread();
	virtual ~FWeftOwnerThread() override;

	/** Runs Work on the owner thread and returns once it has run. */
	void Call(TUniqueFunction<void()> Work);

	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	FRunnableThread* Thread = nullptr;
	FEvent* Ready = nullptr;
	FEvent* Done = nullptr;
	TUniqueFunction<void()> Pending;
	FCriticalSection Calls;
	std::atomic<bool> bStopping{false};
};
