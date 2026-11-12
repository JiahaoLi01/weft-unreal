#include "WeftDebugTools.h"

#if WITH_WEFT_DEVTOOLS
#include "Algo/AllOf.h"
#include "CanvasItem.h"
#include "Containers/Ticker.h"
#include "Debug/DebugDrawService.h"
#include "Debug/WeftDebugHooks.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "Render/WeftFramePacket.h"
#include "UObject/ObjectKey.h"
#include "WeftModule.h"
#include "WeftSubsystem.h"
#include "WeftView.h"
#include "weft/weft.h"

#include <atomic>

/** What the debug tools read of the subsystem and its Views (their friend). */
struct FWeftDebugAccess
{
	static weft_runtime* Runtime(const UWeftSubsystem& Weft)
	{
		return Weft.Runtime;
	}

	/** The Views that have a View in weft. */
	static void Views(const UWeftSubsystem& Weft, TArray<UWeftView*>& Out)
	{
		for (const TWeakObjectPtr<UWeftView>& View : Weft.Views)
		{
			if (UWeftView* Alive = View.Get(); Alive && Alive->IsOpen())
			{
				Out.Add(Alive);
			}
		}
	}

	static weft_runtime* Runtime(const UWeftView& View)
	{
		return View.Runtime;
	}

	static weft_view_handle Handle(const UWeftView& View)
	{
		return {View.RuntimeId, View.Slot, View.Generation};
	}

	static int32 Errors(const UWeftView& View)
	{
		return View.PageErrors;
	}

	static int32 Warnings(const UWeftView& View)
	{
		return View.PageWarnings;
	}
};

namespace
{
	// ===== Developer tools =====

	/** What weft listens on (it tells the port alone): every interface, or this machine's. */
	bool bAllInterfaces = false;

	weft_runtime* RuntimeOf()
	{
		const UWeftSubsystem* Weft = UWeftSubsystem::Get();
		return Weft ? FWeftDebugAccess::Runtime(*Weft) : nullptr;
	}

	void LiveViews(TArray<UWeftView*>& Out)
	{
		if (const UWeftSubsystem* Weft = UWeftSubsystem::Get())
		{
			FWeftDebugAccess::Views(*Weft, Out);
		}
	}

	/** A key of a View's handle, by which the recording thread reports it. */
	uint64 KeyOf(const weft_view_handle& Handle)
	{
		return uint64(Handle.slot) << 32 | Handle.generation;
	}

	/** weft names a View's page in its tools after its handle's slot and generation (weft's src/core/runtime.cpp). */
	FString TargetOf(const UWeftView& View)
	{
		const weft_view_handle Handle = FWeftDebugAccess::Handle(View);
		return FString::Printf(TEXT("%u-%u"), Handle.slot, Handle.generation);
	}

	/** Chrome's address of a View's page: on this machine through the loopback, whatever interfaces weft listens on. */
	FString UrlOf(const UWeftView& View, int32 Port)
	{
		return FString::Printf(TEXT("devtools://devtools/bundled/devtools_app.html?ws=127.0.0.1:%d/devtools/page/%s"), Port,
			*TargetOf(View));
	}

	FString PageOf(const UWeftView& View)
	{
		return View.GetPageName().IsEmpty() ? FString(TEXT("(no page)")) : View.GetPageName();
	}

	void LogUrl(const UWeftView& View, int32 Port)
	{
		UE_LOG(LogWeft, Display, TEXT("Weft DevTools: [%s] %s"), *PageOf(View), *UrlOf(View, Port));
	}

	void LogListening(int32 Port)
	{
		UE_LOG(LogWeft, Display,
			TEXT("Weft DevTools: listening on %s:%d. Open a page's address below in Chrome, or chrome://inspect > Configure > 127.0.0.1:%d."),
			bAllInterfaces ? TEXT("0.0.0.0") : TEXT("127.0.0.1"), Port, Port);
		TArray<UWeftView*> Views;
		LiveViews(Views);
		for (const UWeftView* View : Views)
		{
			if (!View->GetPageName().IsEmpty())
			{
				LogUrl(*View, Port);
			}
		}
	}

	/** "[address:]port": whether every interface (0.0.0.0) or this machine's (127.0.0.1, the default), and the port (0,
	 *  or none: the first free from 9230). False for anything else. */
	bool ParseListen(const FString& Listen, bool& bOutAll, uint32& OutPort)
	{
		FString Address = TEXT("127.0.0.1");
		FString Port = Listen.TrimStartAndEnd();
		int32 Colon = INDEX_NONE;
		if (Port.FindLastChar(TEXT(':'), Colon))
		{
			Address = Port.Left(Colon);
			Port.MidInline(Colon + 1);
		}
		if ((Address != TEXT("127.0.0.1") && Address != TEXT("0.0.0.0")) || Port.Len() > 5 ||
			!Algo::AllOf(Port, [](TCHAR C) { return FChar::IsDigit(C); }))
		{
			return false;
		}
		const int32 Value = Port.IsEmpty() ? 0 : FCString::Atoi(*Port);
		if (Value > 65535)
		{
			return false;
		}
		bOutAll = Address == TEXT("0.0.0.0");
		OutPort = uint32(Value);
		return true;
	}

	// ===== Measuring =====

	/** How long the costs stay measured after something last asked for them, and the window they are averaged over. */
	constexpr double MeasureSeconds = 2.0;
	constexpr double WindowSeconds = 1.0;

	/** A cost per engine frame: what this frame took so far, the window being summed, and the last whole window's
	 *  average and slowest frame in ms. */
	struct FCost
	{
		double Frame = 0;
		double Sum = 0;
		double Slowest = 0;
		float AverageMs = 0.f;
		float SlowestMs = 0.f;

		void EndFrame()
		{
			Sum += Frame;
			Slowest = FMath::Max(Slowest, Frame);
			Frame = 0;
		}

		void EndWindow(int32 Frames)
		{
			AverageMs = Frames > 0 ? float(Sum / Frames * 1000.0) : 0.f;
			SlowestMs = float(Slowest * 1000.0);
			Sum = 0;
			Slowest = 0;
		}
	};

	struct FViewCosts
	{
		FCost Game;
		FCost Draw;
		bool bAnimated = false;
		int32 Elements = -1;
		double ElementsCountedAt = -1.0e9;
	};

	/** The measuring, made when something asks for the costs and let go of when nothing has for MeasureSeconds. Game
	 *  thread. */
	struct FMeasure
	{
		TMap<TObjectKey<UWeftView>, FViewCosts> Views;
		double Until = 0;
		double WindowStart = 0;
		int32 WindowFrames = 0;
		FTSTicker::FDelegateHandle Ticker;
	};
	TUniquePtr<FMeasure> Measure;
	/** Whether Measure is there, for the hooks on any thread. */
	std::atomic<bool> bMeasuring{false};
	/** The draw costs the recording thread reported since the game thread's last tick, by View key; outlives Measure. */
	FCriticalSection DrawLock;
	TMap<uint64, double> Draws;

	/** Elements in a View's document: a script the tools run in the page (weft_view_evaluate). -1 without a page. */
	int32 CountElements(const UWeftView& View)
	{
		weft_runtime* Runtime = FWeftDebugAccess::Runtime(View);
		if (!Runtime || View.GetPageName().IsEmpty())
		{
			return -1;
		}
		static constexpr char Source[] = "document.querySelectorAll('*').length";
		int32 Count = -1;
		weft_view_evaluate(
			Runtime, FWeftDebugAccess::Handle(View), Source, sizeof(Source) - 1,
			[](void* User, uint32 bOk, const char* Text, size_t Size)
			{
				if (bOk)
				{
					const FUTF8ToTCHAR Converted(Text, int32(Size));
					*static_cast<int32*>(User) = FCString::Atoi(*FString(Converted.Length(), Converted.Get()));
				}
			},
			&Count);
		return Count;
	}

	/** Each engine frame while measuring: closes the frame's costs, the window's each second. */
	bool TickMeasure(float)
	{
		FMeasure& State = *Measure;
		const double Now = FPlatformTime::Seconds();
		TMap<uint64, double> Drawn;
		{
			FScopeLock Lock(&DrawLock);
			Drawn = MoveTemp(Draws);
			Draws.Reset();
		}
		TArray<UWeftView*> Views;
		LiveViews(Views);
		for (const UWeftView* View : Views)
		{
			FViewCosts& Costs = State.Views.FindOrAdd(TObjectKey<UWeftView>(View));
			if (const double* Seconds = Drawn.Find(KeyOf(FWeftDebugAccess::Handle(*View))))
			{
				Costs.Draw.Frame += *Seconds;
			}
			Costs.Game.EndFrame();
			Costs.Draw.EndFrame();
		}
		++State.WindowFrames;
		if (Now - State.WindowStart >= WindowSeconds)
		{
			for (const UWeftView* View : Views)
			{
				FViewCosts& Costs = State.Views.FindOrAdd(TObjectKey<UWeftView>(View));
				Costs.Game.EndWindow(State.WindowFrames);
				Costs.Draw.EndWindow(State.WindowFrames);
			}
			for (auto It = State.Views.CreateIterator(); It; ++It)
			{
				if (!It.Key().ResolveObjectPtr())
				{
					It.RemoveCurrent();
				}
			}
			State.WindowStart = Now;
			State.WindowFrames = 0;
		}
		if (Now > State.Until)
		{
			// Nothing asked for a while: no more measuring, until something asks again.
			bMeasuring = false;
			Measure.Reset();
			FScopeLock Lock(&DrawLock);
			Draws.Reset();
			return false;
		}
		return true;
	}

	void KeepMeasuring()
	{
		const double Now = FPlatformTime::Seconds();
		if (!Measure)
		{
			Measure = MakeUnique<FMeasure>();
			Measure->WindowStart = Now;
			Measure->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickMeasure));
			bMeasuring = true;
		}
		Measure->Until = Now + MeasureSeconds;
	}

	void StopMeasuring()
	{
		if (Measure)
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Measure->Ticker);
			Measure.Reset();
		}
		bMeasuring = false;
		FScopeLock Lock(&DrawLock);
		Draws.Reset();
	}

	// ===== The table =====

	/** The table's cells, the header first. */
	TArray<TArray<FString>> Cells(const TArray<FWeftViewDebugInfo>& Views)
	{
		TArray<TArray<FString>> Rows;
		Rows.Add({TEXT("View"), TEXT("Page"), TEXT("Size"), TEXT("Scale"), TEXT("Elements"), TEXT("Game ms"), TEXT("Draw ms"),
			TEXT("Animated"), TEXT("Errors"), TEXT("Warnings")});
		for (const FWeftViewDebugInfo& View : Views)
		{
			Rows.Add({View.Target, View.Page.IsEmpty() ? FString(TEXT("(no page)")) : View.Page,
				FString::Printf(TEXT("%.0fx%.0f"), View.Size.X, View.Size.Y),
				FString::Printf(TEXT("%.2f"), View.DeviceScale),
				View.Elements >= 0 ? FString::FromInt(View.Elements) : FString(TEXT("-")),
				FString::Printf(TEXT("%.2f / %.2f"), View.GameMs, View.GameMaxMs),
				FString::Printf(TEXT("%.2f / %.2f"), View.DrawMs, View.DrawMaxMs),
				View.bAnimated ? TEXT("yes") : TEXT("no"), FString::FromInt(View.Errors), FString::FromInt(View.Warnings)});
		}
		return Rows;
	}

	/** The line above the table: which weft, and where its developer tools are. */
	FString Title(int32 Views)
	{
		const FString Address = FWeftDebugTools::GetDevToolsAddress();
		return FString::Printf(TEXT("weft: %d views, ms per frame as average / slowest over a second.  DevTools: %s"), Views,
			Address.IsEmpty() ? TEXT("off (weft.DevTools)") : *FString::Printf(TEXT("%s (chrome://inspect)"), *Address));
	}

	// ===== On screen =====

	FDelegateHandle Overlay;

	void DrawOverlay(UCanvas* Canvas, APlayerController*)
	{
		if (!Canvas || !GEngine)
		{
			return;
		}
		TArray<FWeftViewDebugInfo> Views;
		FWeftDebugTools::GetViews(Views);
		const TArray<TArray<FString>> Rows = Cells(Views);
		const UFont* Font = GEngine->GetSmallFont();
		// The engine's debug font is sized for 1080 lines.
		const float Scale = FMath::Max(1.f, Canvas->ClipY / 1080.f);
		const float Gap = 14.f * Scale;
		const float Line = Font->GetMaxCharHeight() * Scale * 1.2f;
		TArray<float> Widths;
		Widths.Init(0.f, Rows[0].Num());
		for (const TArray<FString>& Row : Rows)
		{
			for (int32 Column = 0; Column < Row.Num(); ++Column)
			{
				float Width = 0.f;
				float Height = 0.f;
				Canvas->TextSize(Font, Row[Column], Width, Height, Scale, Scale);
				Widths[Column] = FMath::Max(Widths[Column], Width);
			}
		}
		const FString Heading = Title(Views.Num());
		float HeadingWidth = 0.f;
		float HeadingHeight = 0.f;
		Canvas->TextSize(Font, Heading, HeadingWidth, HeadingHeight, Scale, Scale);
		float TableWidth = Gap * (Widths.Num() - 1);
		for (const float Width : Widths)
		{
			TableWidth += Width;
		}
		const FVector2D Origin(16.f * Scale, Canvas->ClipY * 0.12f);
		const FVector2D Pad(8.f * Scale, 6.f * Scale);
		FCanvasTileItem Back(Origin - Pad, FVector2D(FMath::Max(TableWidth, HeadingWidth), Line * (Rows.Num() + 1)) + Pad * 2.f,
			FLinearColor(0.f, 0.f, 0.f, 0.6f));
		Back.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Back);
		FCanvasTextItem Text(Origin, FText::FromString(Heading), Font, FLinearColor::White);
		Text.Scale = FVector2D(Scale, Scale);
		Text.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Text);
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			float X = Origin.X;
			const float Y = Origin.Y + Line * (Index + 1);
			for (int32 Column = 0; Column < Rows[Index].Num(); ++Column)
			{
				FLinearColor Color = Index == 0 ? FLinearColor(0.65f, 0.65f, 0.65f) : FLinearColor::White;
				if (Index > 0 && Column >= 8 && Rows[Index][Column] != TEXT("0"))
				{
					Color = Column == 8 ? FLinearColor(1.f, 0.35f, 0.3f) : FLinearColor(1.f, 0.85f, 0.3f);
				}
				Text.Position = FVector2D(X, Y);
				Text.Text = FText::FromString(Rows[Index][Column]);
				Text.SetColor(Color);
				Canvas->DrawItem(Text);
				X += Widths[Column] + Gap;
			}
		}
	}

	// ===== Console and command line =====

	TAutoConsoleVariable<int32> CVarWeftDebug(TEXT("weft.Debug"), 0,
		TEXT("A table of the weft views on the game's screen (1), or none (0): each view's page, size, scale, elements, ")
		TEXT("what weft costs a frame on the game thread and to record its draw (ms, average / slowest over a second), ")
		TEXT("whether its page animates, its errors and warnings, and where weft's developer tools listen. -WeftDebug on ")
		TEXT("the command line shows it from the start."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
		{
			FWeftDebugTools::ShowOverlay(Variable->GetInt() != 0);
		}),
		ECVF_Default);

	void DevToolsCommand(const TArray<FString>& Arguments)
	{
		if (Arguments.Num() == 1 && Arguments[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			FWeftDebugTools::StopDevTools();
			return;
		}
		if (const int32 Port = FWeftDebugTools::GetDevToolsPort(); Port && Arguments.IsEmpty())
		{
			LogListening(Port);
			return;
		}
		FWeftDebugTools::StartDevTools(Arguments.IsEmpty() ? FString() : Arguments[0]);
	}

	FAutoConsoleCommand DevToolsCommandObject(TEXT("weft.DevTools"),
		TEXT("weft's developer tools: Chrome DevTools on a weft page (its DOM, styles and console). ")
		TEXT("weft.DevTools [[address:]port] starts them, on 127.0.0.1 or 0.0.0.0 (every interface) and the port (none or 0: ")
		TEXT("the first free from 9230), and logs each page's address for Chrome; while they listen, weft.DevTools logs the ")
		TEXT("addresses again. weft.DevTools stop stops them. -WeftDevTools[=[address:]port] on the command line starts them."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DevToolsCommand));

	FAutoConsoleCommand ViewsCommandObject(TEXT("weft.Views"),
		TEXT("Logs a table of the weft views (after measuring their costs for a second): page, size, scale, elements, ms a ")
		TEXT("frame on the game thread and to record the draw, animation, errors and warnings; and each page's address for ")
		TEXT("Chrome while weft's developer tools listen."),
		FConsoleCommandDelegate::CreateLambda([]
		{
			TArray<FWeftViewDebugInfo> Views;
			FWeftDebugTools::GetViews(Views);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				TArray<FWeftViewDebugInfo> Measured;
				FWeftDebugTools::GetViews(Measured);
				UE_LOG(LogWeft, Display, TEXT("%s"), *Title(Measured.Num()));
				for (const FString& Line : FWeftDebugTools::FormatViews(Measured))
				{
					UE_LOG(LogWeft, Display, TEXT("%s"), *Line);
				}
				for (const FWeftViewDebugInfo& View : Measured)
				{
					if (!View.DevToolsUrl.IsEmpty())
					{
						UE_LOG(LogWeft, Display, TEXT("%s [%s] %s"), *View.Target, *View.Page, *View.DevToolsUrl);
					}
				}
				return false;
			}), float(WindowSeconds + 0.1));
		}));
} // namespace

// ===== FWeftDebugTools =====

bool FWeftDebugTools::StartDevTools(const FString& Listen, FString* OutError)
{
	const auto Fail = [OutError](const FString& Why)
	{
		UE_LOG(LogWeft, Warning, TEXT("Weft DevTools: %s"), *Why);
		if (OutError)
		{
			*OutError = Why;
		}
		return false;
	};
	weft_runtime* Runtime = RuntimeOf();
	if (!Runtime)
	{
		return Fail(TEXT("weft does not run here."));
	}
	bool bAll = false;
	uint32 Port = 0;
	if (!ParseListen(Listen, bAll, Port))
	{
		return Fail(FString::Printf(TEXT("\"%s\" is not [127.0.0.1:|0.0.0.0:]port."), *Listen));
	}
	if (GetDevToolsPort() != 0)
	{
		return Fail(FString::Printf(TEXT("they listen on %s already (weft.DevTools stop first)."), *GetDevToolsAddress()));
	}
	const weft_devtools_desc Desc{sizeof(Desc), bAll ? WEFT_DEVTOOLS_ALL_INTERFACES : 0u, Port, 0};
	if (const weft_result Result = weft_devtools_start(Runtime, &Desc); Result != WEFT_OK)
	{
		return Fail(Result == WEFT_BUSY        ? FString::Printf(TEXT("port %u is taken."), Port)
				: Result == WEFT_UNAVAILABLE ? FString(TEXT("this weft.dll is weft's release build, without its developer tools."))
											 : FString(UTF8_TO_TCHAR(weft_result_string(Result))));
	}
	bAllInterfaces = bAll;
	LogListening(GetDevToolsPort());
	return true;
}

void FWeftDebugTools::StopDevTools()
{
	if (weft_runtime* Runtime = RuntimeOf(); Runtime && GetDevToolsPort() != 0)
	{
		weft_devtools_stop(Runtime);
		UE_LOG(LogWeft, Display, TEXT("Weft DevTools: stopped."));
	}
	bAllInterfaces = false;
}

int32 FWeftDebugTools::GetDevToolsPort()
{
	weft_runtime* Runtime = RuntimeOf();
	weft_devtools_info Info{sizeof(Info)};
	return Runtime && weft_devtools_get_info(Runtime, &Info) == WEFT_OK ? int32(Info.port) : 0;
}

FString FWeftDebugTools::GetDevToolsAddress()
{
	const int32 Port = GetDevToolsPort();
	return Port ? FString::Printf(TEXT("%s:%d"), bAllInterfaces ? TEXT("0.0.0.0") : TEXT("127.0.0.1"), Port) : FString();
}

void FWeftDebugTools::GetViews(TArray<FWeftViewDebugInfo>& Out)
{
	Out.Reset();
	TArray<UWeftView*> Views;
	LiveViews(Views);
	if (Views.IsEmpty() && !RuntimeOf())
	{
		return;
	}
	KeepMeasuring();
	const double Now = FPlatformTime::Seconds();
	const int32 Port = GetDevToolsPort();
	for (UWeftView* View : Views)
	{
		FViewCosts& Costs = Measure->Views.FindOrAdd(TObjectKey<UWeftView>(View));
		if (Now - Costs.ElementsCountedAt >= WindowSeconds)
		{
			Costs.Elements = CountElements(*View);
			Costs.ElementsCountedAt = Now;
		}
		FWeftViewDebugInfo& Info = Out.AddDefaulted_GetRef();
		Info.View = View;
		Info.Page = View->GetPageName();
		Info.Target = TargetOf(*View);
		Info.Size = View->GetLogicalSize();
		Info.DeviceScale = View->GetDeviceScale();
		Info.ContentScale = View->GetContentScale();
		Info.Elements = Costs.Elements;
		Info.bAnimated = Costs.bAnimated;
		Info.GameMs = Costs.Game.AverageMs;
		Info.GameMaxMs = Costs.Game.SlowestMs;
		Info.DrawMs = Costs.Draw.AverageMs;
		Info.DrawMaxMs = Costs.Draw.SlowestMs;
		Info.Errors = FWeftDebugAccess::Errors(*View);
		Info.Warnings = FWeftDebugAccess::Warnings(*View);
		Info.DevToolsUrl = Port ? UrlOf(*View, Port) : FString();
	}
}

TArray<FString> FWeftDebugTools::FormatViews(const TArray<FWeftViewDebugInfo>& Views)
{
	const TArray<TArray<FString>> Rows = Cells(Views);
	TArray<int32> Widths;
	Widths.Init(0, Rows[0].Num());
	for (const TArray<FString>& Row : Rows)
	{
		for (int32 Column = 0; Column < Row.Num(); ++Column)
		{
			Widths[Column] = FMath::Max(Widths[Column], Row[Column].Len());
		}
	}
	TArray<FString> Lines;
	for (const TArray<FString>& Row : Rows)
	{
		FString Line;
		for (int32 Column = 0; Column < Row.Num(); ++Column)
		{
			Line += Column + 1 < Row.Num() ? Row[Column].RightPad(Widths[Column] + 2) : Row[Column];
		}
		Lines.Add(MoveTemp(Line));
	}
	return Lines;
}

void FWeftDebugTools::ShowOverlay(bool bShow)
{
	if (bShow && !Overlay.IsValid())
	{
		Overlay = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&DrawOverlay));
	}
	else if (!bShow && Overlay.IsValid())
	{
		UDebugDrawService::Unregister(Overlay);
		Overlay.Reset();
	}
}

bool FWeftDebugTools::IsOverlayShown()
{
	return Overlay.IsValid();
}

// ===== Hooks =====

bool WeftDebug::IsMeasuring()
{
	return bMeasuring.load(std::memory_order_relaxed);
}

void WeftDebug::AddGameCost(const UWeftView& View, uint64 Cycles)
{
	if (Measure)
	{
		Measure->Views.FindOrAdd(TObjectKey<UWeftView>(&View)).Game.Frame += FPlatformTime::ToSeconds64(Cycles);
	}
}

void WeftDebug::AddDrawCost(const FWeftFramePacket& Packet, uint64 Cycles)
{
	FScopeLock Lock(&DrawLock);
	Draws.FindOrAdd(KeyOf(Packet.View)) += FPlatformTime::ToSeconds64(Cycles);
}

void WeftDebug::OnFrame(const UWeftView& View, const weft_view_frame* Frame)
{
	weft_view_frame_info Info{sizeof(Info)};
	if (Measure && Frame && weft_view_frame_get_info(Frame, &Info) == WEFT_OK)
	{
		Measure->Views.FindOrAdd(TObjectKey<UWeftView>(&View)).bAnimated = Info.animated != 0;
	}
}

void WeftDebug::OnPageLoaded(const UWeftView& View)
{
	if (const int32 Port = FWeftDebugTools::GetDevToolsPort())
	{
		LogUrl(View, Port);
	}
}

void WeftDebug::OnRuntimeStarted(UWeftSubsystem&)
{
	FString Listen;
	if (FParse::Value(FCommandLine::Get(), TEXT("WeftDevTools="), Listen))
	{
		FWeftDebugTools::StartDevTools(Listen);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("WeftDevTools")))
	{
		FWeftDebugTools::StartDevTools();
	}
	else if (const int32 Port = FWeftDebugTools::GetDevToolsPort())
	{
		// weft's own WEFT_DEVTOOLS_LISTEN started them with the runtime.
		bAllInterfaces = FPlatformMisc::GetEnvironmentVariable(TEXT("WEFT_DEVTOOLS_LISTEN")).StartsWith(TEXT("0.0.0.0"));
		LogListening(Port);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("WeftDebug")))
	{
		CVarWeftDebug->Set(1, ECVF_SetByCommandline);
	}
}

void WeftDebug::OnRuntimeEnding(UWeftSubsystem&)
{
	FWeftDebugTools::ShowOverlay(false);
	StopMeasuring();
	bAllInterfaces = false;
}

#endif
