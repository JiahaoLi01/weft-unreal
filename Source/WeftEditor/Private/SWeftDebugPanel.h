#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

#if WITH_WEFT_DEVTOOLS
#include "WeftDebugTools.h"

class SEditableTextBox;

/**
 * The Weft panel (Window > Log > Weft): weft's developer tools (start and stop them, each page's address for Chrome) and
 * the table of views (FWeftDebugTools::GetViews), twice a second while the panel shows. The costs are measured only
 * while it shows.
 */
class SWeftDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWeftDebugPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FItem = TSharedPtr<FWeftViewDebugInfo>;

	EActiveTimerReturnType Refresh(double CurrentTime, float DeltaTime);
	TSharedRef<ITableRow> MakeRow(FItem Item, const TSharedRef<STableViewBase>& Owner);
	FReply OnDevToolsButton();
	FText GetDevToolsStatus() const;
	FText GetDevToolsButtonText() const;

	TArray<FItem> Items;
	TSharedPtr<SListView<FItem>> List;
	TSharedPtr<SEditableTextBox> Listen;
	/** Why the developer tools did not start, when they did not. */
	FText Error;
};
#endif
