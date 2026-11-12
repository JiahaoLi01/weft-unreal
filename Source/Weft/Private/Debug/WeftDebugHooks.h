#pragma once

#include "CoreMinimal.h"

// Where the plugin's classes tell its debug tools (WeftDebugTools.cpp) what they did: the macros at the end are empty
// in a build without weft's developer tools (WITH_WEFT_DEVTOOLS 0), so that nothing of the tools is in it.

#if WITH_WEFT_DEVTOOLS
#include "HAL/PlatformTime.h"

class UWeftSubsystem;
class UWeftView;
struct FWeftFramePacket;
struct weft_view_frame;

namespace WeftDebug
{
	/** Whether the tools measure the Views' costs now: something showed them in the last two seconds. Any thread. */
	bool IsMeasuring();
	/** Time weft took for a View on the game thread (FPlatformTime cycles). */
	void AddGameCost(const UWeftView& View, uint64 Cycles);
	/** Time a View's frame took to record, on the thread that records it. Any thread. */
	void AddDrawCost(const FWeftFramePacket& Packet, uint64 Cycles);
	/** A View handed a frame over: whether its page animates. */
	void OnFrame(const UWeftView& View, const weft_view_frame* Frame);
	/** A View loaded a page: where its developer tools are, while they listen. */
	void OnPageLoaded(const UWeftView& View);
	/** The runtime started (the command line's -WeftDevTools and -WeftDebug), or is ending. */
	void OnRuntimeStarted(UWeftSubsystem& Weft);
	void OnRuntimeEnding(UWeftSubsystem& Weft);

	/** Adds the time of its scope to a View's game-thread cost while the tools measure. */
	struct FGameCost
	{
		explicit FGameCost(const UWeftView& InView)
			: View(InView), Start(IsMeasuring() ? FPlatformTime::Cycles64() : 0)
		{
		}

		~FGameCost()
		{
			if (Start)
			{
				AddGameCost(View, FPlatformTime::Cycles64() - Start);
			}
		}

		const UWeftView& View;
		const uint64 Start;
	};

	/** Adds the time of its scope to the cost of recording a packet's frame while the tools measure. */
	struct FDrawCost
	{
		explicit FDrawCost(const FWeftFramePacket& InPacket)
			: Packet(InPacket), Start(IsMeasuring() ? FPlatformTime::Cycles64() : 0)
		{
		}

		~FDrawCost()
		{
			if (Start)
			{
				AddDrawCost(Packet, FPlatformTime::Cycles64() - Start);
			}
		}

		const FWeftFramePacket& Packet;
		const uint64 Start;
	};
} // namespace WeftDebug

#define WEFT_DEBUG_HOOK(Call) WeftDebug::Call
#define WEFT_DEBUG_GAME_COST(View) const WeftDebug::FGameCost WeftDebugGameCost(View)
#define WEFT_DEBUG_DRAW_COST(Packet) const WeftDebug::FDrawCost WeftDebugDrawCost(Packet)
#else
#define WEFT_DEBUG_HOOK(Call)
#define WEFT_DEBUG_GAME_COST(View)
#define WEFT_DEBUG_DRAW_COST(Packet)
#endif
