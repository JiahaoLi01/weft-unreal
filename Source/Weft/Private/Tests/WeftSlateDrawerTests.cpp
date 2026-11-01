// A layer's drawer and Slate's draw: the drawer SWeftViewport's OnPaint hands a frame's elements outlives its layer,
// and its widget, to the end of the frame, by when Slate has batched those elements (its batcher keeps a plain pointer
// to the drawer) and enqueued their draw; then the render thread lets go of it. A page's message, which the widget's
// Tick delivers inside Slate's paint, may close the page between the two.
//
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Weft.SlateDrawer; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_WEFT

#include "Input/HittestGrid.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "RenderingThread.h"
#include "SWeftViewport.h"
#include "Styling/WidgetStyle.h"
#include "Types/PaintArgs.h"
#include "WeftSubsystem.h"
#include "WeftTypes.h"
#include "WeftView.h"
#include "Widgets/SWindow.h"

// The module's files share unity translation units: each test file keeps its helpers in a namespace of its own.
namespace WeftSlateDrawerTests
{
namespace
{
	using FWeakDrawer = TWeakPtr<ICustomSlateElement, ESPMode::ThreadSafe>;

	// A View of a page with a box, kept from the garbage collector, with a frame to draw.
	UWeftView* NewView()
	{
		UWeftView* View = UWeftSubsystem::Get()->CreateView();
		View->AddToRoot();
		View->Resize(FVector2D(64, 32), 1);
		FWeftPage Page;
		Page.Scripts.Emplace(TEXT("page.js"),
			TEXT("document.getElementById('root').appendChild(document.createElement('div'));"));
		Page.Stylesheet = TEXT(
			"#root{position:relative;width:100%;height:100%;margin:0}"
			"div{width:16px;height:16px;background:red}");
		Page.bForgivingCss = true;
		View->Load(Page);
		View->Advance();
		return View;
	}

	// The widget's paint, as Slate's draw paints it after its Tick: the drawers it handed the window's elements, by
	// layer.
	TArray<FWeakDrawer> Paint(const SWeftViewport& Widget, FSlateWindowElementList& Elements)
	{
		FHittestGrid Grid;
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(64, 32), FSlateLayoutTransform());
		Widget.OnPaint(FPaintArgs(nullptr, Grid, FVector2D::ZeroVector, 0.0, 0.f), Geometry, FSlateRect(0, 0, 64, 32),
			Elements, 0, FWidgetStyle(), true);
		TArray<FWeakDrawer> Drawers;
		for (const FSlateCustomDrawerElement& Element :
			Elements.GetUncachedDrawElements().Get<(uint8)EElementType::ET_Custom>())
		{
			Drawers.Add(Element.CustomDrawer);
		}
		return Drawers;
	}
} // namespace

// Once the frame the drawers were let go of in has ended: the render thread has let go of them.
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FWeftDrawersGone, FAutomationTestBase*, Test, uint64, Frame,
	TArray<FWeakDrawer>, Drawers);

bool FWeftDrawersGone::Update()
{
	if (GFrameCounter == Frame)
	{
		return false;
	}
	FlushRenderingCommands();
	for (const FWeakDrawer& Drawer : Drawers)
	{
		Test->TestFalse(TEXT("the render thread lets go of a drawer after the frame's end"), Drawer.IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftSlateDrawerReleaseTest, "Weft.SlateDrawer.Release",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftSlateDrawerReleaseTest::RunTest(const FString& Parameters)
{
	const UWeftSubsystem* Weft = UWeftSubsystem::Get();
	if (!Weft || !Weft->IsAvailable())
	{
		AddError(TEXT("weft does not run here"));
		return false;
	}
	UWeftView* First = NewView();
	UWeftView* Second = NewView();
	TSharedPtr<SWeftViewport> Widget = SNew(SWeftViewport).View(First);
	Widget->AddView(Second, 1);
	const TSharedRef<SWindow> Window = SNew(SWindow);
	FSlateWindowElementList Elements(Window);
	const TArray<FWeakDrawer> Drawers = Paint(*Widget, Elements);
	if (TestEqual(TEXT("each layer hands the frame's elements its drawer"), Drawers.Num(), 2))
	{
		// The second page's layer, then the widget, go in the middle of Slate's draw, before Slate batches the
		// elements (as when a page's message closes its page); the render thread runs everything enqueued so far.
		Widget->RemoveView(Second);
		Widget.Reset();
		FlushRenderingCommands();
		TestTrue(TEXT("a removed layer's drawer outlives it to the frame's end"), Drawers[1].IsValid());
		TestTrue(TEXT("a widget's drawer outlives it to the frame's end"), Drawers[0].IsValid());
		ADD_LATENT_AUTOMATION_COMMAND(FWeftDrawersGone(this, GFrameCounter, Drawers));
	}
	First->RemoveFromRoot();
	Second->RemoveFromRoot();
	return true;
}

} // namespace WeftSlateDrawerTests

#endif
