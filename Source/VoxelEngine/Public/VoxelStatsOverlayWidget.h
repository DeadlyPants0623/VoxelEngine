#pragma once

#include "Blueprint/UserWidget.h"
#include "VoxelStatsOverlayWidget.generated.h"

class UTextBlock;

UCLASS()
class VOXELENGINE_API UVoxelStatsOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetGenerationStats(const FString& InHeaderText, const FString& InPrimaryMetricsText, const FString& InSecondaryMetricsText);

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildOverlay();
	void RefreshDisplay();

	UPROPERTY(Transient)
	UTextBlock* HeaderTextBlock = nullptr;

	UPROPERTY(Transient)
	UTextBlock* PrimaryMetricsTextBlock = nullptr;

	UPROPERTY(Transient)
	UTextBlock* SecondaryMetricsTextBlock = nullptr;

	FString HeaderText = TEXT("VOXEL TERRAIN GENERATION");
	FString PrimaryMetricsText = TEXT("Waiting for GenerateChunks");
	FString SecondaryMetricsText;
};
