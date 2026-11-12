#include "SWeftDebugPanel.h"

#if WITH_WEFT_DEVTOOLS
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WeftEditor"

namespace
{
	// How often the panel reads the views again.
	constexpr float RefreshSeconds = 0.5f;

	const FName ViewColumn(TEXT("View"));
	const FName PageColumn(TEXT("Page"));
	const FName SizeColumn(TEXT("Size"));
	const FName ScaleColumn(TEXT("Scale"));
	const FName ElementsColumn(TEXT("Elements"));
	const FName GameColumn(TEXT("Game"));
	const FName DrawColumn(TEXT("Draw"));
	const FName AnimatedColumn(TEXT("Animated"));
	const FName ErrorsColumn(TEXT("Errors"));
	const FName WarningsColumn(TEXT("Warnings"));
	const FName DevToolsColumn(TEXT("DevTools"));

	/** A view's row: its numbers, and its page's address for Chrome while weft's developer tools listen. */
	class SWeftViewRow : public SMultiColumnTableRow<TSharedPtr<FWeftViewDebugInfo>>
	{
	public:
		SLATE_BEGIN_ARGS(SWeftViewRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&, const TSharedRef<STableViewBase>& Owner, TSharedPtr<FWeftViewDebugInfo> InItem)
		{
			Item = MoveTemp(InItem);
			SMultiColumnTableRow::Construct(FSuperRowType::FArguments().Padding(FMargin(0.f, 2.f)), Owner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			const FWeftViewDebugInfo& View = *Item;
			if (Column == DevToolsColumn)
			{
				return SNew(SBox).HAlign(HAlign_Left)
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyAddress", "Copy address"))
					.ToolTipText(View.DevToolsUrl.IsEmpty() ? LOCTEXT("CopyAddressOff", "weft's developer tools do not listen.")
						: FText::Format(LOCTEXT("CopyAddressTip", "Paste into Chrome's address bar: {0}"), FText::FromString(View.DevToolsUrl)))
					.IsEnabled(!View.DevToolsUrl.IsEmpty())
					.OnClicked_Lambda([Url = View.DevToolsUrl]
					{
						FPlatformApplicationMisc::ClipboardCopy(*Url);
						return FReply::Handled();
					})
				];
			}
			FString Text;
			FSlateColor Color = FSlateColor::UseForeground();
			if (Column == ViewColumn)
			{
				Text = View.Target;
			}
			else if (Column == PageColumn)
			{
				Text = View.Page.IsEmpty() ? FString(TEXT("(no page)")) : View.Page;
			}
			else if (Column == SizeColumn)
			{
				Text = FString::Printf(TEXT("%.0f x %.0f"), View.Size.X, View.Size.Y);
			}
			else if (Column == ScaleColumn)
			{
				Text = FString::Printf(TEXT("%.2f"), View.DeviceScale);
			}
			else if (Column == ElementsColumn)
			{
				Text = View.Elements >= 0 ? FString::FromInt(View.Elements) : FString(TEXT("-"));
			}
			else if (Column == GameColumn)
			{
				Text = FString::Printf(TEXT("%.2f / %.2f"), View.GameMs, View.GameMaxMs);
			}
			else if (Column == DrawColumn)
			{
				Text = FString::Printf(TEXT("%.2f / %.2f"), View.DrawMs, View.DrawMaxMs);
			}
			else if (Column == AnimatedColumn)
			{
				Text = View.bAnimated ? TEXT("yes") : TEXT("no");
			}
			else if (Column == ErrorsColumn)
			{
				Text = FString::FromInt(View.Errors);
				Color = View.Errors ? FSlateColor(FLinearColor(1.f, 0.3f, 0.25f)) : Color;
			}
			else if (Column == WarningsColumn)
			{
				Text = FString::FromInt(View.Warnings);
				Color = View.Warnings ? FSlateColor(FLinearColor(1.f, 0.8f, 0.25f)) : Color;
			}
			return SNew(STextBlock).Text(FText::FromString(Text)).ColorAndOpacity(Color).Margin(FMargin(4.f, 0.f));
		}

	private:
		TSharedPtr<FWeftViewDebugInfo> Item;
	};
} // namespace

void SWeftDebugPanel::Construct(const FArguments&)
{
	const auto Column = [](const FName& Id, const FText& Label, float Width, const FText& Tip = FText::GetEmpty())
	{
		return SHeaderRow::Column(Id).DefaultLabel(Label).DefaultTooltip(Tip).FillWidth(Width);
	};
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(STextBlock).Text(LOCTEXT("DevToolsLabel", "DevTools"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SBox).WidthOverride(150.f)
					[
						SAssignNew(Listen, SEditableTextBox)
						.HintText(LOCTEXT("ListenHint", "9230, or 0.0.0.0:9230"))
						.ToolTipText(LOCTEXT("ListenTip", "[address:]port: 127.0.0.1 (this machine) or 0.0.0.0 (every interface); empty or 0 for the first free port from 9230."))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SButton)
					.Text(this, &SWeftDebugPanel::GetDevToolsButtonText)
					.OnClicked(this, &SWeftDebugPanel::OnDevToolsButton)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SWeftDebugPanel::GetDevToolsStatus).AutoWrapText(true)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("OverlayTip", "The same table on the game's screen (weft.Debug)."))
					.IsChecked_Lambda([] { return FWeftDebugTools::IsOverlayShown() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([](ECheckBoxState State)
					{
						if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("weft.Debug")))
						{
							Variable->Set(State == ECheckBoxState::Checked ? 1 : 0, ECVF_SetByConsole);
						}
					})
					[
						SNew(STextBlock).Text(LOCTEXT("Overlay", "On screen"))
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return Error; })
				.ColorAndOpacity(FLinearColor(1.f, 0.3f, 0.25f))
				.Visibility_Lambda([this] { return Error.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SAssignNew(List, SListView<FItem>)
				.ListItemsSource(&Items)
				.OnGenerateRow(this, &SWeftDebugPanel::MakeRow)
				.SelectionMode(ESelectionMode::None)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ Column(ViewColumn, LOCTEXT("ViewColumn", "View"), 0.5f,
						LOCTEXT("ViewTip", "The view's page in weft's developer tools: /devtools/page/<slot>-<generation>."))
					+ Column(PageColumn, LOCTEXT("PageColumn", "Page"), 1.0f, LOCTEXT("PageTip", "The folder of the page's stylesheet."))
					+ Column(SizeColumn, LOCTEXT("SizeColumn", "Size"), 0.9f, LOCTEXT("SizeTip", "Logical (CSS) px."))
					+ Column(ScaleColumn, LOCTEXT("ScaleColumn", "Scale"), 0.5f, LOCTEXT("ScaleTip", "Device px per logical px."))
					+ Column(ElementsColumn, LOCTEXT("ElementsColumn", "Elements"), 0.6f, LOCTEXT("ElementsTip", "Elements in the page's document."))
					+ Column(GameColumn, LOCTEXT("GameColumn", "Game ms"), 0.9f,
						LOCTEXT("GameTip", "weft's work for the view on the game thread (its frame, input, the frame handed to the renderer), ms per engine frame: average / slowest over the last second."))
					+ Column(DrawColumn, LOCTEXT("DrawColumn", "Draw ms"), 0.9f,
						LOCTEXT("DrawTip", "Recording the view's draw on the RHI thread, ms per engine frame: average / slowest over the last second."))
					+ Column(AnimatedColumn, LOCTEXT("AnimatedColumn", "Animated"), 0.6f,
						LOCTEXT("AnimatedTip", "Whether the page's frames change by themselves (CSS animations and transitions, requestAnimationFrame)."))
					+ Column(ErrorsColumn, LOCTEXT("ErrorsColumn", "Errors"), 0.5f,
						LOCTEXT("ErrorsTip", "Exceptions and console errors since the page loaded (LogWeftPage)."))
					+ Column(WarningsColumn, LOCTEXT("WarningsColumn", "Warnings"), 0.6f,
						LOCTEXT("WarningsTip", "Styles the page's scene cannot take, and console warnings, since the page loaded (LogWeftPage)."))
					+ Column(DevToolsColumn, LOCTEXT("DevToolsColumn", "DevTools"), 1.2f,
						LOCTEXT("DevToolsTip", "The page's address for Chrome while weft's developer tools listen."))
				)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Footer", "Costs are measured while this panel or the on-screen table shows. Pages' errors and warnings are in the Output Log under LogWeftPage."))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]
		]
	];
	RegisterActiveTimer(RefreshSeconds, FWidgetActiveTimerDelegate::CreateSP(this, &SWeftDebugPanel::Refresh));
	Refresh(0.0, 0.f);
}

EActiveTimerReturnType SWeftDebugPanel::Refresh(double, float)
{
	TArray<FWeftViewDebugInfo> Views;
	FWeftDebugTools::GetViews(Views);
	Items.Reset(Views.Num());
	for (FWeftViewDebugInfo& View : Views)
	{
		Items.Add(MakeShared<FWeftViewDebugInfo>(MoveTemp(View)));
	}
	if (List)
	{
		List->RequestListRefresh();
	}
	return EActiveTimerReturnType::Continue;
}

TSharedRef<ITableRow> SWeftDebugPanel::MakeRow(FItem Item, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(SWeftViewRow, Owner, Item);
}

FReply SWeftDebugPanel::OnDevToolsButton()
{
	Error = FText::GetEmpty();
	if (FWeftDebugTools::GetDevToolsPort() != 0)
	{
		FWeftDebugTools::StopDevTools();
	}
	else
	{
		FString Why;
		if (!FWeftDebugTools::StartDevTools(Listen ? Listen->GetText().ToString() : FString(), &Why))
		{
			Error = FText::FromString(Why);
		}
	}
	Refresh(0.0, 0.f);
	return FReply::Handled();
}

FText SWeftDebugPanel::GetDevToolsStatus() const
{
	const FString Address = FWeftDebugTools::GetDevToolsAddress();
	if (Address.IsEmpty())
	{
		return LOCTEXT("DevToolsOff", "Off. Start them to inspect a page in Chrome (its DOM, styles and console).");
	}
	return FText::Format(LOCTEXT("DevToolsOn", "Listening on {0}. In Chrome: copy a page's address below into the address bar, or chrome://inspect > Configure > 127.0.0.1:{1}."),
		FText::FromString(Address), FText::AsNumber(FWeftDebugTools::GetDevToolsPort(), &FNumberFormattingOptions::DefaultNoGrouping()));
}

FText SWeftDebugPanel::GetDevToolsButtonText() const
{
	return FWeftDebugTools::GetDevToolsPort() != 0 ? LOCTEXT("Stop", "Stop") : LOCTEXT("Start", "Start");
}

#undef LOCTEXT_NAMESPACE

#endif
