#include "VoxelStatsOverlayWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void UVoxelStatsOverlayWidget::SetGenerationStats(const FString& InHeaderText, const FString& InPrimaryMetricsText, const FString& InSecondaryMetricsText)
{
	HeaderText = InHeaderText;
	PrimaryMetricsText = InPrimaryMetricsText;
	SecondaryMetricsText = InSecondaryMetricsText;
	RefreshDisplay();
}

void UVoxelStatsOverlayWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetVisibility(ESlateVisibility::HitTestInvisible);
	BuildOverlay();
	RefreshDisplay();
}

void UVoxelStatsOverlayWidget::BuildOverlay()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UBorder* StatsPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StatsPanel"));
	StatsPanel->SetPadding(FMargin(20.0f, 16.0f));
	StatsPanel->SetBrushColor(FLinearColor(0.025f, 0.042f, 0.072f, 0.90f));

	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(StatsPanel);
	PanelSlot->SetAutoSize(true);
	PanelSlot->SetPosition(FVector2D(42.0f, 42.0f));

	UVerticalBox* ContentBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ContentBox"));
	StatsPanel->SetContent(ContentBox);

	HeaderTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HeaderText"));
	HeaderTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.96f, 0.98f, 1.0f)));
	HeaderTextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	HeaderTextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	{
		FSlateFontInfo HeaderFont = HeaderTextBlock->GetFont();
		HeaderFont.Size = 22;
		HeaderFont.TypefaceFontName = TEXT("Bold");
		HeaderTextBlock->SetFont(HeaderFont);
	}
	UVerticalBoxSlot* HeaderSlot = ContentBox->AddChildToVerticalBox(HeaderTextBlock);
	HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

	PrimaryMetricsTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PrimaryMetricsText"));
	PrimaryMetricsTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.95f, 0.99f)));
	PrimaryMetricsTextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
	PrimaryMetricsTextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	{
		FSlateFontInfo PrimaryFont = PrimaryMetricsTextBlock->GetFont();
		PrimaryFont.Size = 15;
		PrimaryMetricsTextBlock->SetFont(PrimaryFont);
	}
	UVerticalBoxSlot* PrimarySlot = ContentBox->AddChildToVerticalBox(PrimaryMetricsTextBlock);
	PrimarySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	SecondaryMetricsTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SecondaryMetricsText"));
	SecondaryMetricsTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.73f, 0.80f, 0.90f)));
	SecondaryMetricsTextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.30f));
	SecondaryMetricsTextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	{
		FSlateFontInfo SecondaryFont = SecondaryMetricsTextBlock->GetFont();
		SecondaryFont.Size = 14;
		SecondaryMetricsTextBlock->SetFont(SecondaryFont);
	}
	ContentBox->AddChildToVerticalBox(SecondaryMetricsTextBlock);
}

void UVoxelStatsOverlayWidget::RefreshDisplay()
{
	if (HeaderTextBlock != nullptr)
	{
		HeaderTextBlock->SetText(FText::FromString(HeaderText));
	}

	if (PrimaryMetricsTextBlock != nullptr)
	{
		PrimaryMetricsTextBlock->SetText(FText::FromString(PrimaryMetricsText));
	}

	if (SecondaryMetricsTextBlock != nullptr)
	{
		SecondaryMetricsTextBlock->SetText(FText::FromString(SecondaryMetricsText));
	}
}
