#include "Voxel.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "ConvexVolume.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"
#include "SceneView.h"
#include "VoxelChunk.h"
#include "VoxelProxyChunk.h"
#include "VoxelStatsOverlayWidget.h"

namespace
{
int64 GetVoxelSlotsPerChunk(int32 ChunkGridSize, int32 ChunkHeight)
{
	return static_cast<int64>(ChunkGridSize) * ChunkGridSize * ChunkHeight;
}

int32 GetAutoScaledWorkerThreadCount()
{
	return FMath::Max(1, FPlatformMisc::NumberOfWorkerThreadsToSpawn());
}

FString FormatDuration(const FTimespan& Duration)
{
	return FString::Printf(TEXT("%.2f ms (%.3f s)"), Duration.GetTotalMilliseconds(), Duration.GetTotalSeconds());
}

FString FormatDurationShortMs(const FTimespan& Duration)
{
	return FString::Printf(TEXT("%.1f ms"), Duration.GetTotalMilliseconds());
}

int32 FloorDivide(int32 Value, int32 Divisor)
{
	check(Divisor > 0);

	if (Value >= 0)
	{
		return Value / Divisor;
	}

	return -(((-Value) + Divisor - 1) / Divisor);
}

int64 EstimateMeshBytes(const FVoxelChunkMeshData& MeshData)
{
	return static_cast<int64>(MeshData.Vertices.GetAllocatedSize())
		+ MeshData.Triangles.GetAllocatedSize()
		+ MeshData.Normals.GetAllocatedSize()
		+ MeshData.UVs.GetAllocatedSize()
		+ MeshData.VertexColors.GetAllocatedSize()
		+ MeshData.Tangents.GetAllocatedSize();
}

void AppendTranslatedMeshData(FVoxelChunkMeshData& OutMeshData, const FVoxelChunkMeshData& InMeshData, const FVector& Translation)
{
	const int32 BaseIndex = OutMeshData.Vertices.Num();
	OutMeshData.Vertices.Reserve(OutMeshData.Vertices.Num() + InMeshData.Vertices.Num());
	for (const FVector& Vertex : InMeshData.Vertices)
	{
		OutMeshData.Vertices.Add(Vertex + Translation);
	}

	OutMeshData.Triangles.Reserve(OutMeshData.Triangles.Num() + InMeshData.Triangles.Num());
	for (const int32 TriangleIndex : InMeshData.Triangles)
	{
		OutMeshData.Triangles.Add(BaseIndex + TriangleIndex);
	}

	OutMeshData.Normals.Append(InMeshData.Normals);
	OutMeshData.UVs.Append(InMeshData.UVs);
	OutMeshData.VertexColors.Append(InMeshData.VertexColors);
	OutMeshData.Tangents.Append(InMeshData.Tangents);
	OutMeshData.Stats.SolidVoxelCount += InMeshData.Stats.SolidVoxelCount;
	OutMeshData.Stats.SurfaceVoxelCount += InMeshData.Stats.SurfaceVoxelCount;
	OutMeshData.Stats.CulledInteriorVoxelCount += InMeshData.Stats.CulledInteriorVoxelCount;
	OutMeshData.Stats.FaceCount += InMeshData.Stats.FaceCount;
	OutMeshData.Stats.TriangleCount += InMeshData.Stats.TriangleCount;
	OutMeshData.Stats.VertexCount += InMeshData.Stats.VertexCount;
}

struct FProxyBuildChunkInput
{
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;
	FVector Translation = FVector::ZeroVector;
	TSharedPtr<FVoxelChunkMeshData, ESPMode::ThreadSafe> CachedMeshData;
};

}

static TAutoConsoleVariable<int32> CVarVoxelStatsOverlayMode(
	TEXT("r.Voxel.StatsOverlayMode"),
	0,
	TEXT("Voxel stats HUD: 0 = compact (default), 1 = full diagnostics. Toggle in-game: F10."),
	ECVF_Default);

AVoxel::AVoxel()
	: NumChunks(4)
	, ChunkGridSize(32)
	, VoxelSize(75.f)
	, ChunkHeight(240)
	, WorldSeed(1337)
	, GenerationTime(FTimespan::Zero())
	, LastVoxelBuildTime(FTimespan::Zero())
	, LastMeshBuildTime(FTimespan::Zero())
	, LastChunkApplyTime(FTimespan::Zero())
	, GeneratedChunkCount(0)
	, TotalVoxelSlots(0)
	, TotalSolidVoxels(0)
	, TotalSurfaceVoxels(0)
	, TotalCulledInteriorVoxels(0)
	, TotalVisibleFaces(0)
	, TotalTriangles(0)
	, TotalVertices(0)
	, QueuedChunkCount(0)
	, GeneratingChunkCount(0)
	, FinalizedChunksThisTick(0)
	, DroppedStaleChunkResultCount(0)
	, DroppedStaleVoxelResultCount(0)
	, DroppedStaleMeshResultCount(0)
	, RemeshedChunkCount(0)
	, VisibleLoadedChunkCount(0)
	, HiddenLoadedChunkCount(0)
	, VisibleQueuedChunkCount(0)
	, ActiveProxyGroupCount(0)
	, QueuedProxyGroupCount(0)
	, BuildingProxyGroupCount(0)
	, VisibleProxyGroupCount(0)
	, ProxyTransitionsThisTick(0)
	, ChunkRenderVisibilityTransitionsThisTick(0)
	, ProxyMeshBytes(0)
	, ActiveRealChunkJobCountStat(0)
	, ActiveProxyJobCountStat(0)
	, FirstChunkBuildCount(0)
	, DroppedOutdatedResultCount(0)
	, DeferredProxyJobStartCount(0)
	, DeferredVoxelJobStartDueToMeshQueueCount(0)
	, MinSampledTerrainHeight(0)
	, MaxSampledTerrainHeight(0)
	, AverageSampledTerrainHeight(0.0f)
	, LoadRadius(5)
	, UnloadRadius(8)
	, StreamingUpdateInterval(0.1f)
	, bAutoScaleStreamingBudgets(true)
	, MaxConcurrentGenerationJobs(4)
	, MaxQueuedJobStartsPerTick(4)
	, MaxChunkFinalizationsPerTick(3)
	, MaxCompletedVoxelBuildsPerTick(8)
	, MaxDeferredChunkRemeshFlushesPerTick(32)
	, MaxMeshSpawnFinalizationsPerTick(2)
	, MeshQueueVoxelBackpressureThreshold(0)
	, ViewCullingGracePeriod(0.15f)
	, ChunkViewRenderExtentPaddingCm(250.0f)
	, ChunkViewRenderHorizontalExtentMultiplier(1.08f)
	, ChunkViewRenderAngularSpeedPaddingScale(4.0f)
	, ChunkViewRenderLinearSpeedPaddingScale(0.025f)
	, MaxChunkRenderVisibilityTogglesPerTick(32)
	, OutOfViewChunkJobPriorityPenalty(650000)
	, MaxConcurrentProxyJobs(1)
	, StatsRefreshInterval(0.2f)
	, ProxyGroupChunkSpan(4)
	, ProxyRealChunkRadius(2)
	, ProxyOnlyStartRadius(4)
	, CollisionChunkRadius(2)
	, SceneRoot(nullptr)
	, StatsOverlayWidget(nullptr)
	, LastStreamedChunkCoord(FIntPoint::ZeroValue)
	, bHasStreamedChunkCoord(false)
	, CurrentStreamingEpoch(0)
	, NextChunkRequestId(1)
	, LastStreamingRefreshTimeSeconds(TNumericLimits<double>::Lowest())
	, LastStatsRefreshTimeSeconds(TNumericLimits<double>::Lowest())
	, MeshSpawnFinalizationsThisTick(0)
	, LastViewBiasCameraForward(FVector::ForwardVector)
	, bHasLastViewBiasSample(false)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("GenerateChunks"),
		TEXT("Rebuilds streamed chunks around the active camera"),
		FConsoleCommandDelegate::CreateUObject(this, &AVoxel::CreateChunks),
		ECVF_Cheat
	);
}

void AVoxel::PostLoad()
{
	Super::PostLoad();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	// Older shipped default (7000): once the mesh queue stayed near this size, voxel jobs stopped entirely (deadlock with heavy exploration).
	if (MeshQueueVoxelBackpressureThreshold == 7000)
	{
		MeshQueueVoxelBackpressureThreshold = 0;
	}

	if (OutOfViewChunkJobPriorityPenalty <= 0)
	{
		OutOfViewChunkJobPriorityPenalty = 650000;
	}
}

void AVoxel::BeginPlay()
{
	Super::BeginPlay();

	PrimaryActorTick.TickInterval = 0.0f;
	LastStreamingRefreshTimeSeconds = TNumericLimits<double>::Lowest();
	LastStatsRefreshTimeSeconds = TNumericLimits<double>::Lowest();

	ResetGenerationStats();
	GenerationStatusText = TEXT("Status            Waiting for camera focus");
	UpdateStreamingChunks(true);
	StartQueuedVoxelJobs();
	StartQueuedMeshJobs();
	StartQueuedProxyMeshJobs();
	UpdateRenderArbitration();
	RefreshChunkStats();
	LastStatsOverlayDisplayMode = CVarVoxelStatsOverlayMode.GetValueOnGameThread();
	UpdateGenerationStatsDisplay();
	LastStatsRefreshTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
}

void AVoxel::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (DeltaTime > KINDA_SMALL_NUMBER)
	{
		const float FrameMs = DeltaTime * 1000.0f;
		if (!bStatsOverlaySmoothedFrameMsInit)
		{
			StatsOverlaySmoothedFrameMs = FrameMs;
			bStatsOverlaySmoothedFrameMsInit = true;
		}
		else
		{
			constexpr float FrameMsSmoothAlpha = 0.12f;
			StatsOverlaySmoothedFrameMs = FMath::Lerp(StatsOverlaySmoothedFrameMs, FrameMs, FrameMsSmoothAlpha);
		}
	}

	FinalizedChunksThisTick = 0;
	ProxyTransitionsThisTick = 0;
	ChunkRenderVisibilityTransitionsThisTick = 0;
	MeshSpawnFinalizationsThisTick = 0;
	ActiveRealChunkJobCountStat = GetActiveRealChunkJobCount();
	ActiveProxyJobCountStat = GetActiveProxyJobCount();

	if (ShouldRefreshStreamingChunks())
	{
		UpdateStreamingChunks();
	}
	else
	{
		UpdateChunkVisibilityStates();
	}

	ProcessCompletedVoxelBuilds();
	ProcessCompletedMeshBuilds();
	ProcessCompletedProxyMeshBuilds();
	ProcessDeferredChunkRemeshes();
	StartQueuedVoxelJobs();
	StartQueuedMeshJobs();
	StartQueuedProxyMeshJobs();
	UpdateRenderArbitration();
	ActiveRealChunkJobCountStat = GetActiveRealChunkJobCount();
	ActiveProxyJobCountStat = GetActiveProxyJobCount();
	const int32 StatsModeNow = CVarVoxelStatsOverlayMode.GetValueOnGameThread();
	const bool bStatsModeChanged = (StatsModeNow != LastStatsOverlayDisplayMode);
	const bool bNeedChunkStatRefresh = ShouldRefreshStats() || bStatsModeChanged;
	if (bNeedChunkStatRefresh)
	{
		if (bStatsModeChanged)
		{
			LastStatsOverlayDisplayMode = StatsModeNow;
		}
		RefreshChunkStats();
		UpdateGenerationStatsDisplay();
		LastStatsRefreshTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	}
	else if (StatsModeNow == 0)
	{
		UpdateGenerationStatsDisplay();
	}
}

void AVoxel::ForceRefreshGenerationStatsDisplay()
{
	LastStatsOverlayDisplayMode = CVarVoxelStatsOverlayMode.GetValueOnGameThread();
	RefreshChunkStats();
	UpdateGenerationStatsDisplay();
	LastStatsRefreshTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
}

void AVoxel::ClearChunks()
{
	for (TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		if (Entry.Value.ChunkActor.IsValid())
		{
			Entry.Value.ChunkActor->Destroy();
		}
	}

	for (TPair<FIntPoint, FVoxelProxyGroupRuntimeEntry>& Entry : ProxyGroupRuntimeEntries)
	{
		if (Entry.Value.ProxyActor.IsValid())
		{
			Entry.Value.ProxyActor->Destroy();
		}
	}

	ChunkRuntimeEntries.Empty();
	ProxyGroupRuntimeEntries.Empty();
	QueuedVoxelChunkCoords.Empty();
	QueuedMeshChunkCoords.Empty();
	QueuedProxyGroupCoords.Empty();
	InFlightRequestIds.Empty();
	ActiveVoxelRequestIds.Empty();
	ActiveMeshRequestIds.Empty();
	ActiveProxyRequestIds.Empty();
	DeferredChunkRemeshCoords.Empty();

	FVoxelChunkVoxelBuildResult PendingVoxelResult;
	while (CompletedVoxelResults.Dequeue(PendingVoxelResult))
	{
	}

	FVoxelChunkMeshBuildResult PendingMeshResult;
	while (CompletedMeshResults.Dequeue(PendingMeshResult))
	{
	}

	FVoxelProxyMeshBuildResult PendingProxyResult;
	while (CompletedProxyMeshResults.Dequeue(PendingProxyResult))
	{
	}

	bHasStreamedChunkCoord = false;
	CurrentStreamingEpoch = 0;
	RefreshChunkStats();
}

void AVoxel::ResetGenerationStats()
{
	GenerationTime = FTimespan::Zero();
	LastVoxelBuildTime = FTimespan::Zero();
	LastMeshBuildTime = FTimespan::Zero();
	LastChunkApplyTime = FTimespan::Zero();
	GeneratedChunkCount = 0;
	TotalVoxelSlots = 0;
	TotalSolidVoxels = 0;
	TotalSurfaceVoxels = 0;
	TotalCulledInteriorVoxels = 0;
	TotalVisibleFaces = 0;
	TotalTriangles = 0;
	TotalVertices = 0;
	QueuedChunkCount = 0;
	GeneratingChunkCount = 0;
	FinalizedChunksThisTick = 0;
	DroppedStaleChunkResultCount = 0;
	DroppedStaleVoxelResultCount = 0;
	DroppedStaleMeshResultCount = 0;
	RemeshedChunkCount = 0;
	VisibleLoadedChunkCount = 0;
	HiddenLoadedChunkCount = 0;
	VisibleQueuedChunkCount = 0;
	ActiveProxyGroupCount = 0;
	QueuedProxyGroupCount = 0;
	BuildingProxyGroupCount = 0;
	VisibleProxyGroupCount = 0;
	ProxyTransitionsThisTick = 0;
	ProxyMeshBytes = 0;
	ActiveRealChunkJobCountStat = 0;
	ActiveProxyJobCountStat = 0;
	FirstChunkBuildCount = 0;
	DroppedOutdatedResultCount = 0;
	DeferredProxyJobStartCount = 0;
	DeferredVoxelJobStartDueToMeshQueueCount = 0;
	MeshSpawnFinalizationsThisTick = 0;
	MinSampledTerrainHeight = 0;
	MaxSampledTerrainHeight = 0;
	AverageSampledTerrainHeight = 0.0f;
	GenerationStatsSummary = TEXT("VOXEL TERRAIN GENERATION\nPreparing stream...");
}

void AVoxel::EnsureStatsOverlay()
{
	if (StatsOverlayWidget != nullptr || GetWorld() == nullptr)
	{
		return;
	}

	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		return;
	}

	StatsOverlayWidget = CreateWidget<UVoxelStatsOverlayWidget>(PlayerController, UVoxelStatsOverlayWidget::StaticClass());
	if (StatsOverlayWidget != nullptr)
	{
		StatsOverlayWidget->AddToViewport(1000);
	}
}

void AVoxel::UpdateGenerationStatsDisplay()
{
	EnsureStatsOverlay();

	const auto FormatNumber = [](int64 Value) -> FString
	{
		return FText::AsNumber(Value).ToString();
	};

	const FString TrianglesText = FormatNumber(TotalTriangles);
	const int32 StatsMode = CVarVoxelStatsOverlayMode.GetValueOnGameThread();
	const bool bFullStats = (StatsMode != 0);
	const float SmoothedFps = (StatsOverlaySmoothedFrameMs > KINDA_SMALL_NUMBER) ? (1000.0f / StatsOverlaySmoothedFrameMs) : 0.0f;

	FString HeaderText;
	FString PrimaryMetricsText;
	FString SecondaryMetricsText;

	if (!bFullStats)
	{
		HeaderText = TEXT("VOXEL TERRAIN (compact)");
		PrimaryMetricsText = FString::Printf(
			TEXT("%s\n")
			TEXT("Frame              %.1f ms  (~%.0f FPS)\n")
			TEXT("Queued / Gen       %d / %d\n")
			TEXT("Real / Proxy jobs  %d / %d\n")
			TEXT("Voxel / Mesh / Ap  %s / %s / %s\n")
			TEXT("Triangles          %s\n")
			TEXT("Loaded chunks      %d"),
			*GenerationStatusText,
			StatsOverlaySmoothedFrameMs,
			SmoothedFps,
			QueuedChunkCount,
			GeneratingChunkCount,
			ActiveRealChunkJobCountStat,
			ActiveProxyJobCountStat,
			*FormatDurationShortMs(LastVoxelBuildTime),
			*FormatDurationShortMs(LastMeshBuildTime),
			*FormatDurationShortMs(LastChunkApplyTime),
			*TrianglesText,
			GeneratedChunkCount);
		SecondaryMetricsText = TEXT("F10: full stats   Console: r.Voxel.StatsOverlayMode 1");
	}
	else
	{
		const FString DenseSlotsText = FormatNumber(TotalVoxelSlots);
		const FString SolidVoxelsText = FormatNumber(TotalSolidVoxels);
		const FString SurfaceVoxelsText = FormatNumber(TotalSurfaceVoxels);
		const FString CulledInteriorText = FormatNumber(TotalCulledInteriorVoxels);
		const FString VisibleFacesText = FormatNumber(TotalVisibleFaces);
		const FString VerticesText = FormatNumber(TotalVertices);
		const FString ProxyMeshBytesText = FormatNumber(ProxyMeshBytes);
		const int32 EffectiveUnloadRadius = GetEffectiveUnloadRadius();
		const int32 LoadDiameter = (LoadRadius * 2) + 1;

		HeaderText = TEXT("VOXEL TERRAIN GENERATION");
		PrimaryMetricsText = FString::Printf(
			TEXT("%s\n")
			TEXT("Frame              %.1f ms  (~%.0f FPS)\n")
			TEXT("Loaded Chunks     %d\n")
			TEXT("Visible Loaded    %d\n")
			TEXT("Hidden Loaded     %d\n")
			TEXT("Queued Chunks     %d\n")
			TEXT("Visible Queued    %d\n")
			TEXT("Proxy Groups      %d\n")
			TEXT("Visible Proxies   %d\n")
			TEXT("Real Jobs         %d\n")
			TEXT("Proxy Jobs        %d\n")
			TEXT("Generating        %d\n")
			TEXT("Collision Rad     %d\n")
			TEXT("Load Radius       %d\n")
			TEXT("Unload Radius     %d\n")
			TEXT("Load Footprint    %d x %d chunks\n")
			TEXT("World Seed        %d\n")
			TEXT("Base / Amp        %.1f / %.1f\n")
			TEXT("Freq / Octaves    %.4f / %d\n")
			TEXT("Grid / Chunk      %d x %d x %d\n")
			TEXT("Voxel Size        %.0f cm\n")
			TEXT("Voxel Build       %s\n")
			TEXT("Mesh Build        %s\n")
			TEXT("Mesh Apply        %s"),
			*GenerationStatusText,
			StatsOverlaySmoothedFrameMs,
			SmoothedFps,
			GeneratedChunkCount,
			VisibleLoadedChunkCount,
			HiddenLoadedChunkCount,
			QueuedChunkCount,
			VisibleQueuedChunkCount,
			ActiveProxyGroupCount,
			VisibleProxyGroupCount,
			ActiveRealChunkJobCountStat,
			ActiveProxyJobCountStat,
			GeneratingChunkCount,
			CollisionChunkRadius,
			LoadRadius,
			EffectiveUnloadRadius,
			LoadDiameter,
			LoadDiameter,
			WorldSeed,
			TerrainSettings.BaseHeight,
			TerrainSettings.HeightAmplitude,
			TerrainSettings.NoiseFrequency,
			TerrainSettings.Octaves,
			ChunkGridSize,
			ChunkGridSize,
			ChunkHeight,
			VoxelSize,
			*FormatDuration(LastVoxelBuildTime),
			*FormatDuration(LastMeshBuildTime),
			*FormatDuration(LastChunkApplyTime));

		SecondaryMetricsText = FString::Printf(
			TEXT("Finalized / Tick  %d\n")
			TEXT("Remesh Count      %d\n")
			TEXT("Stale Voxel       %d\n")
			TEXT("Stale Mesh        %d\n")
			TEXT("Dropped Total     %d\n")
			TEXT("Proxy Queued      %d\n")
			TEXT("Proxy Building    %d\n")
			TEXT("Proxy Switches    %d\n")
			TEXT("Chunk Rend Sw     %d\n")
			TEXT("Max Rend Sw/Tick  %d\n")
			TEXT("View Pad Base cm  %.0f\n")
			TEXT("Horiz Mult        %.2f\n")
			TEXT("Out-V Job Penalty %d\n")
			TEXT("Proxy Mesh Bytes  %s\n")
			TEXT("First Chunk Builds %d\n")
			TEXT("Dropped Outdated  %d\n")
			TEXT("Deferred Proxy    %d\n")
			TEXT("Defer Voxel/MeshQ %d\n")
			TEXT("Sea / Mountain    %.1f / %.2f\n")
			TEXT("Persist / Lacun   %.2f / %.2f\n")
			TEXT("Height Range      %d .. %d (avg %.1f)\n")
			TEXT("Dense Slots       %s\n")
			TEXT("Solid Voxels      %s\n")
			TEXT("Surface Voxels    %s\n")
			TEXT("Interior Culled   %s\n")
			TEXT("Visible Faces     %s\n")
			TEXT("Triangles         %s\n")
			TEXT("Vertices          %s"),
			FinalizedChunksThisTick,
			RemeshedChunkCount,
			DroppedStaleVoxelResultCount,
			DroppedStaleMeshResultCount,
			DroppedStaleChunkResultCount,
			QueuedProxyGroupCount,
			BuildingProxyGroupCount,
			ProxyTransitionsThisTick,
			ChunkRenderVisibilityTransitionsThisTick,
			MaxChunkRenderVisibilityTogglesPerTick,
			ChunkViewRenderExtentPaddingCm,
			ChunkViewRenderHorizontalExtentMultiplier,
			OutOfViewChunkJobPriorityPenalty,
			*ProxyMeshBytesText,
			FirstChunkBuildCount,
			DroppedOutdatedResultCount,
			DeferredProxyJobStartCount,
			DeferredVoxelJobStartDueToMeshQueueCount,
			TerrainSettings.SeaLevel,
			TerrainSettings.MountainStrength,
			TerrainSettings.Persistence,
			TerrainSettings.Lacunarity,
			MinSampledTerrainHeight,
			MaxSampledTerrainHeight,
			AverageSampledTerrainHeight,
			*DenseSlotsText,
			*SolidVoxelsText,
			*SurfaceVoxelsText,
			*CulledInteriorText,
			*VisibleFacesText,
			*TrianglesText,
			*VerticesText);
	}

	GenerationStatsSummary = FString::Printf(
		TEXT("%s\n")
		TEXT("%s\n")
		TEXT("%s"),
		*HeaderText,
		*PrimaryMetricsText,
		*SecondaryMetricsText);

	if (StatsOverlayWidget != nullptr)
	{
		StatsOverlayWidget->SetGenerationStats(HeaderText, PrimaryMetricsText, SecondaryMetricsText);
	}
}

void AVoxel::CreateChunks()
{
	ClearChunks();
	ResetGenerationStats();
	LastStreamingRefreshTimeSeconds = TNumericLimits<double>::Lowest();
	LastStatsRefreshTimeSeconds = TNumericLimits<double>::Lowest();
	GenerationStatusText = TEXT("Status            Rebuilding streamed terrain...");
	UpdateStreamingChunks(true);
	StartQueuedVoxelJobs();
	StartQueuedMeshJobs();
	StartQueuedProxyMeshJobs();
	UpdateRenderArbitration();
	RefreshChunkStats();
	UpdateGenerationStatsDisplay();
	LastStatsRefreshTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
}

void AVoxel::UpdateStreamingChunks(bool bForceRefresh)
{
	const UWorld* World = GetWorld();
	LastStreamingRefreshTimeSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;

	FVector FocusLocation = FVector::ZeroVector;
	if (!TryGetStreamingFocusLocation(FocusLocation))
	{
		if (bForceRefresh)
		{
			GenerationStatusText = TEXT("Status            Waiting for camera focus");
		}

		return;
	}

	const FIntPoint CurrentChunkCoord = GetChunkCoordFromWorldLocation(FocusLocation);
	const bool bShouldRefreshStreamingSet = bForceRefresh || !bHasStreamedChunkCoord || CurrentChunkCoord != LastStreamedChunkCoord;
	if (!bShouldRefreshStreamingSet)
	{
		UpdateChunkVisibilityStates();
		return;
	}

	LastStreamedChunkCoord = CurrentChunkCoord;
	bHasStreamedChunkCoord = true;
	++CurrentStreamingEpoch;

	const double RequestTimeSeconds = LastStreamingRefreshTimeSeconds;
	for (TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		Entry.Value.bIsDesired = false;
	}

	for (TPair<FIntPoint, FVoxelProxyGroupRuntimeEntry>& Entry : ProxyGroupRuntimeEntries)
	{
		Entry.Value.bIsDesired = false;
	}

	for (int32 DeltaX = -LoadRadius; DeltaX <= LoadRadius; ++DeltaX)
	{
		for (int32 DeltaY = -LoadRadius; DeltaY <= LoadRadius; ++DeltaY)
		{
			QueueChunkRequest(FIntPoint(CurrentChunkCoord.X + DeltaX, CurrentChunkCoord.Y + DeltaY), RequestTimeSeconds);
		}
	}

	TArray<FIntPoint> ChunkCoordsToRemove;
	const int32 EffectiveUnloadRadius = GetEffectiveUnloadRadius();
	for (TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		const int32 DeltaX = Entry.Key.X - CurrentChunkCoord.X;
		const int32 DeltaY = Entry.Key.Y - CurrentChunkCoord.Y;
		const bool bOutsideUnloadRadius = FMath::Abs(DeltaX) > EffectiveUnloadRadius || FMath::Abs(DeltaY) > EffectiveUnloadRadius;
		if (!bOutsideUnloadRadius)
		{
			Entry.Value.bIsDesired = true;
			continue;
		}

		if (Entry.Value.State == EVoxelChunkRuntimeState::Ready || Entry.Value.State == EVoxelChunkRuntimeState::Queued || Entry.Value.State == EVoxelChunkRuntimeState::PendingMeshing)
		{
			ChunkCoordsToRemove.Add(Entry.Key);
			continue;
		}
	}

	for (const FIntPoint& ChunkCoord : ChunkCoordsToRemove)
	{
		UnloadChunk(ChunkCoord);
	}

	UpdateProxyGroupDesiredStates(RequestTimeSeconds);
	GenerationStatusText = FString::Printf(TEXT("Status            Streaming around chunk (%d, %d)"), CurrentChunkCoord.X, CurrentChunkCoord.Y);
	UpdateChunkVisibilityStates();
}

void AVoxel::EnqueueVoxelChunk(const FIntPoint& ChunkCoord)
{
	if (FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord))
	{
		if (!RuntimeEntry->bVoxelQueued)
		{
			RuntimeEntry->bVoxelQueued = true;
			QueuedVoxelChunkCoords.Add(ChunkCoord);
		}
	}
}

void AVoxel::EnqueueMeshChunk(const FIntPoint& ChunkCoord)
{
	if (FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord))
	{
		if (!RuntimeEntry->bMeshQueued)
		{
			RuntimeEntry->bMeshQueued = true;
			QueuedMeshChunkCoords.Add(ChunkCoord);
		}
	}
}

void AVoxel::EnqueueProxyGroup(const FIntPoint& GroupCoord)
{
	if (FVoxelProxyGroupRuntimeEntry* RuntimeEntry = ProxyGroupRuntimeEntries.Find(GroupCoord))
	{
		if (!RuntimeEntry->bProxyQueued)
		{
			RuntimeEntry->bProxyQueued = true;
			QueuedProxyGroupCoords.Add(GroupCoord);
		}
	}
}

void AVoxel::QueueChunkRequest(const FIntPoint& ChunkCoord, double RequestTimeSeconds)
{
	FVoxelChunkRuntimeEntry* ExistingEntry = ChunkRuntimeEntries.Find(ChunkCoord);
	if (ExistingEntry != nullptr)
	{
		ExistingEntry->bIsDesired = true;
		ExistingEntry->LastRequestedTimeSeconds = RequestTimeSeconds;
		ExistingEntry->LastRequestedStreamingEpoch = CurrentStreamingEpoch;

		if (ExistingEntry->State == EVoxelChunkRuntimeState::Ready && !ExistingEntry->ChunkActor.IsValid())
		{
			if (ExistingEntry->CachedVoxels.IsValid())
			{
				RequestDeferredChunkRemesh(ChunkCoord);
			}
			else
			{
				ExistingEntry->State = EVoxelChunkRuntimeState::Queued;
				ExistingEntry->ActiveRequestId = 0;
				EnqueueVoxelChunk(ChunkCoord);
			}
		}

		return;
	}

	FVoxelChunkRuntimeEntry& NewEntry = ChunkRuntimeEntries.Add(ChunkCoord);
	NewEntry.State = EVoxelChunkRuntimeState::Queued;
	NewEntry.LastRequestedTimeSeconds = RequestTimeSeconds;
	NewEntry.LastRequestedStreamingEpoch = CurrentStreamingEpoch;
	NewEntry.bIsDesired = true;
	EnqueueVoxelChunk(ChunkCoord);
}

void AVoxel::QueueProxyGroupRequest(const FIntPoint& GroupCoord, double RequestTimeSeconds)
{
	FVoxelProxyGroupRuntimeEntry* ExistingEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
	if (ExistingEntry != nullptr)
	{
		ExistingEntry->bIsDesired = true;
		ExistingEntry->LastRequestedTimeSeconds = RequestTimeSeconds;
		ExistingEntry->LastRequestedStreamingEpoch = CurrentStreamingEpoch;

		if (ExistingEntry->State == EVoxelProxyGroupRuntimeState::Ready && !ExistingEntry->ProxyActor.IsValid())
		{
			ExistingEntry->bNeedsRebuild = true;
			ExistingEntry->State = EVoxelProxyGroupRuntimeState::Queued;
			ExistingEntry->ActiveRequestId = 0;
			EnqueueProxyGroup(GroupCoord);
		}

		return;
	}

	FVoxelProxyGroupRuntimeEntry& NewEntry = ProxyGroupRuntimeEntries.Add(GroupCoord);
	NewEntry.State = EVoxelProxyGroupRuntimeState::Queued;
	NewEntry.LastRequestedTimeSeconds = RequestTimeSeconds;
	NewEntry.LastRequestedStreamingEpoch = CurrentStreamingEpoch;
	NewEntry.bIsDesired = true;
	NewEntry.bNeedsRebuild = true;
	NewEntry.Revision = 1;
	EnqueueProxyGroup(GroupCoord);
}

void AVoxel::StartQueuedVoxelJobs()
{
	if (MeshQueueVoxelBackpressureThreshold > 0 && QueuedMeshChunkCoords.Num() >= MeshQueueVoxelBackpressureThreshold)
	{
		++DeferredVoxelJobStartDueToMeshQueueCount;
		return;
	}

	int32 JobsStartedThisTick = 0;
	const int32 JobStartBudget = GetQueuedJobStartBudget();
	const int32 GenerationJobLimit = GetGenerationJobLimit();
	while (JobsStartedThisTick < JobStartBudget && GetActiveRealChunkJobCount() < GenerationJobLimit)
	{
		FIntPoint ChunkCoord = FIntPoint::ZeroValue;
		if (!TryPopNextQueuedVoxelChunk(ChunkCoord))
		{
			break;
		}

		FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
		if (RuntimeEntry == nullptr || RuntimeEntry->State != EVoxelChunkRuntimeState::Queued || !RuntimeEntry->bIsDesired)
		{
			continue;
		}

		if (RuntimeEntry->ActiveRequestId == 0)
		{
			RuntimeEntry->ActiveRequestId = ReserveRequestId();
		}

		const uint64 RequestId = RuntimeEntry->ActiveRequestId;
		const uint32 RequestStreamingEpoch = RuntimeEntry->LastRequestedStreamingEpoch;
		RuntimeEntry->State = EVoxelChunkRuntimeState::GeneratingVoxels;
		RuntimeEntry->ActiveRequestStreamingEpoch = RequestStreamingEpoch;
		InFlightRequestIds.Add(RequestId);
		ActiveVoxelRequestIds.Add(RequestId);

		const int32 LocalGridSize = ChunkGridSize;
		const int32 LocalChunkHeight = ChunkHeight;
		const int32 LocalWorldSeed = WorldSeed;
		const FVoxelTerrainSettings LocalTerrainSettings = TerrainSettings;
		const FVector2D ChunkOffset = GetChunkOffset(ChunkCoord);
		const double StartTimeSeconds = FPlatformTime::Seconds();
		const TWeakObjectPtr<AVoxel> WeakVoxel(this);

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakVoxel, ChunkCoord, RequestId, RequestStreamingEpoch, LocalGridSize, LocalChunkHeight, LocalWorldSeed, LocalTerrainSettings, ChunkOffset, StartTimeSeconds]()
			{
				TArray<AVoxelChunk::FVoxel> GeneratedVoxels;
				FVoxelTerrainSampleStats TerrainStats;
				FVoxelChunkBorderData BorderData;
				AVoxelChunk::BuildVoxelData(GeneratedVoxels, TerrainStats, BorderData, LocalGridSize, LocalChunkHeight, ChunkOffset, LocalTerrainSettings, LocalWorldSeed);

				if (!WeakVoxel.IsValid())
				{
					return;
				}

				FVoxelChunkVoxelBuildResult BuildResult;
				BuildResult.ChunkCoord = ChunkCoord;
				BuildResult.RequestId = RequestId;
				BuildResult.RequestStreamingEpoch = RequestStreamingEpoch;
				BuildResult.Voxels = MoveTemp(GeneratedVoxels);
				BuildResult.BorderData = MoveTemp(BorderData);
				BuildResult.BuildDuration = FTimespan::FromSeconds(FPlatformTime::Seconds() - StartTimeSeconds);
				BuildResult.TerrainStats = TerrainStats;

				WeakVoxel->CompletedVoxelResults.Enqueue(MoveTemp(BuildResult));
			});

		++JobsStartedThisTick;
	}
}

void AVoxel::StartQueuedMeshJobs()
{
	int32 JobsStartedThisTick = 0;
	const int32 JobStartBudget = GetQueuedJobStartBudget();
	const int32 GenerationJobLimit = GetGenerationJobLimit();
	while (JobsStartedThisTick < JobStartBudget && GetActiveRealChunkJobCount() < GenerationJobLimit)
	{
		FIntPoint ChunkCoord = FIntPoint::ZeroValue;
		if (!TryPopNextQueuedMeshChunk(ChunkCoord))
		{
			break;
		}

		FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
		if (RuntimeEntry == nullptr
			|| RuntimeEntry->State != EVoxelChunkRuntimeState::PendingMeshing
			|| !RuntimeEntry->bIsDesired
			|| !RuntimeEntry->CachedVoxels.IsValid()
			|| !RuntimeEntry->bNeedsRemesh)
		{
			continue;
		}

		if (RuntimeEntry->ActiveRequestId == 0)
		{
			RuntimeEntry->ActiveRequestId = ReserveRequestId();
			InFlightRequestIds.Add(RuntimeEntry->ActiveRequestId);
		}

		const uint64 RequestId = RuntimeEntry->ActiveRequestId;
		const uint32 RequestStreamingEpoch = RuntimeEntry->LastRequestedStreamingEpoch;
		const TSharedPtr<TArray<AVoxelChunk::FVoxel>, ESPMode::ThreadSafe> CachedVoxels = RuntimeEntry->CachedVoxels;
		const FVoxelChunkNeighborBorderData NeighborBorderData = GetNeighborBorderDataForChunk(ChunkCoord);
		const int32 LocalGridSize = ChunkGridSize;
		const int32 LocalChunkHeight = ChunkHeight;
		const float LocalVoxelSize = VoxelSize;
		const double StartTimeSeconds = FPlatformTime::Seconds();
		const TWeakObjectPtr<AVoxel> WeakVoxel(this);

		RuntimeEntry->State = EVoxelChunkRuntimeState::Meshing;
		RuntimeEntry->bNeedsRemesh = false;
		RuntimeEntry->ActiveRequestStreamingEpoch = RequestStreamingEpoch;
		ActiveMeshRequestIds.Add(RequestId);

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakVoxel, ChunkCoord, RequestId, RequestStreamingEpoch, CachedVoxels, NeighborBorderData, LocalGridSize, LocalChunkHeight, LocalVoxelSize, StartTimeSeconds]()
			{
				FVoxelChunkMeshBuildResult BuildResult;
				BuildResult.ChunkCoord = ChunkCoord;
				BuildResult.RequestId = RequestId;
				BuildResult.RequestStreamingEpoch = RequestStreamingEpoch;

				if (CachedVoxels.IsValid())
				{
					AVoxelChunk::BuildMeshData(BuildResult.MeshData, *CachedVoxels, LocalGridSize, LocalChunkHeight, LocalVoxelSize, NeighborBorderData);
				}

				if (!WeakVoxel.IsValid())
				{
					return;
				}

				BuildResult.BuildDuration = FTimespan::FromSeconds(FPlatformTime::Seconds() - StartTimeSeconds);
				WeakVoxel->CompletedMeshResults.Enqueue(MoveTemp(BuildResult));
			});

		++JobsStartedThisTick;
	}
}

void AVoxel::StartQueuedProxyMeshJobs()
{
	if (GetActiveRealChunkJobCount() > 0 || HasPendingRealChunkWork())
	{
		++DeferredProxyJobStartCount;

		return;
	}

	if (GetActiveRealChunkJobCount() >= GetGenerationJobLimit())
	{
		++DeferredProxyJobStartCount;

		return;
	}

	int32 JobsStartedThisTick = 0;
	const int32 JobStartBudget = GetQueuedJobStartBudget();
	const int32 ProxyJobLimit = GetProxyJobLimit();
	while (JobsStartedThisTick < JobStartBudget && GetActiveProxyJobCount() < ProxyJobLimit)
	{
		FIntPoint GroupCoord = FIntPoint::ZeroValue;
		if (!TryPopNextQueuedProxyGroup(GroupCoord))
		{
			break;
		}

		FVoxelProxyGroupRuntimeEntry* RuntimeEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
		if (RuntimeEntry == nullptr || !RuntimeEntry->bIsDesired || !RuntimeEntry->bNeedsRebuild)
		{
			continue;
		}

		RuntimeEntry->ReadySourceChunkCount = GetReadySourceChunkCountForProxyGroup(GroupCoord);
		const int32 DesiredSourceChunkCount = GetDesiredSourceChunkCountForProxyGroup(GroupCoord);
		if (DesiredSourceChunkCount <= 0 || RuntimeEntry->ReadySourceChunkCount < DesiredSourceChunkCount)
		{
			continue;
		}

		TArray<FProxyBuildChunkInput> BuildInputs;
		BuildInputs.Reserve(ProxyGroupChunkSpan * ProxyGroupChunkSpan);

		const FIntPoint GroupOriginChunkCoord = GetProxyGroupOriginChunkCoord(GroupCoord);
		const float LocalChunkWorldSize = GetChunkWorldSize();
		for (int32 LocalX = 0; LocalX < ProxyGroupChunkSpan; ++LocalX)
		{
			for (int32 LocalY = 0; LocalY < ProxyGroupChunkSpan; ++LocalY)
			{
				const FIntPoint ChunkCoord(GroupOriginChunkCoord.X + LocalX, GroupOriginChunkCoord.Y + LocalY);
				const FVoxelChunkRuntimeEntry* ChunkEntry = ChunkRuntimeEntries.Find(ChunkCoord);
				if (ChunkEntry == nullptr
					|| !ChunkEntry->bIsDesired
					|| ChunkEntry->State != EVoxelChunkRuntimeState::Ready
					|| !ChunkEntry->CachedMeshData.IsValid())
				{
					continue;
				}

				FProxyBuildChunkInput& BuildInput = BuildInputs.AddDefaulted_GetRef();
				BuildInput.ChunkCoord = ChunkCoord;
				BuildInput.Translation = FVector(
					static_cast<float>(ChunkCoord.X - GroupOriginChunkCoord.X) * LocalChunkWorldSize,
					static_cast<float>(ChunkCoord.Y - GroupOriginChunkCoord.Y) * LocalChunkWorldSize,
					0.0f);
				BuildInput.CachedMeshData = ChunkEntry->CachedMeshData;
			}
		}

		if (BuildInputs.Num() == 0)
		{
			continue;
		}

		if (RuntimeEntry->ActiveRequestId == 0)
		{
			RuntimeEntry->ActiveRequestId = ReserveRequestId();
		}

		const uint64 RequestId = RuntimeEntry->ActiveRequestId;
		const uint32 Revision = RuntimeEntry->Revision;
		const uint32 RequestStreamingEpoch = RuntimeEntry->LastRequestedStreamingEpoch;
		const double StartTimeSeconds = FPlatformTime::Seconds();
		const TWeakObjectPtr<AVoxel> WeakVoxel(this);

		RuntimeEntry->State = EVoxelProxyGroupRuntimeState::Meshing;
		RuntimeEntry->bNeedsRebuild = false;
		RuntimeEntry->ActiveBuildRevision = Revision;
		RuntimeEntry->ActiveRequestStreamingEpoch = RequestStreamingEpoch;
		ActiveProxyRequestIds.Add(RequestId);

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakVoxel, GroupCoord, RequestId, RequestStreamingEpoch, Revision, BuildInputs, StartTimeSeconds]()
			{
				FVoxelProxyMeshBuildResult BuildResult;
				BuildResult.GroupCoord = GroupCoord;
				BuildResult.RequestId = RequestId;
				BuildResult.RequestStreamingEpoch = RequestStreamingEpoch;
				BuildResult.Revision = Revision;
				BuildResult.SourceChunkCount = BuildInputs.Num();

				for (const FProxyBuildChunkInput& BuildInput : BuildInputs)
				{
					if (!BuildInput.CachedMeshData.IsValid())
					{
						continue;
					}

					AppendTranslatedMeshData(BuildResult.MeshData, *BuildInput.CachedMeshData, BuildInput.Translation);
				}

				if (!WeakVoxel.IsValid())
				{
					return;
				}

				BuildResult.ApproximateMeshBytes = EstimateMeshBytes(BuildResult.MeshData);
				BuildResult.BuildDuration = FTimespan::FromSeconds(FPlatformTime::Seconds() - StartTimeSeconds);
				WeakVoxel->CompletedProxyMeshResults.Enqueue(MoveTemp(BuildResult));
			});

		++JobsStartedThisTick;
	}
}

void AVoxel::ProcessCompletedVoxelBuilds()
{
	const int32 CompletionBudget = FMath::Max(1, MaxCompletedVoxelBuildsPerTick);
	int32 DequeueCount = 0;
	FVoxelChunkVoxelBuildResult BuildResult;
	while (DequeueCount < CompletionBudget && CompletedVoxelResults.Dequeue(BuildResult))
	{
		++DequeueCount;
		ActiveVoxelRequestIds.Remove(BuildResult.RequestId);

		FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(BuildResult.ChunkCoord);
		if (RuntimeEntry == nullptr
			|| RuntimeEntry->State != EVoxelChunkRuntimeState::GeneratingVoxels
			|| RuntimeEntry->ActiveRequestId != BuildResult.RequestId)
		{
			InFlightRequestIds.Remove(BuildResult.RequestId);
			++DroppedStaleVoxelResultCount;
			++DroppedStaleChunkResultCount;
			continue;
		}

		if (!RuntimeEntry->bIsDesired)
		{
			ChunkRuntimeEntries.Remove(BuildResult.ChunkCoord);
			InFlightRequestIds.Remove(BuildResult.RequestId);
			++DroppedStaleVoxelResultCount;
			++DroppedStaleChunkResultCount;
			continue;
		}

		RuntimeEntry->CachedVoxels = MakeShared<TArray<AVoxelChunk::FVoxel>, ESPMode::ThreadSafe>();
		*RuntimeEntry->CachedVoxels = MoveTemp(BuildResult.Voxels);
		RuntimeEntry->CachedMeshData.Reset();
		RuntimeEntry->BorderData = MoveTemp(BuildResult.BorderData);
		RuntimeEntry->TerrainStats = BuildResult.TerrainStats;
		RuntimeEntry->State = EVoxelChunkRuntimeState::PendingMeshing;
		RuntimeEntry->bNeedsRemesh = true;
		LastVoxelBuildTime = BuildResult.BuildDuration;
		GenerationTime = LastVoxelBuildTime;
		RuntimeEntry->ActiveRequestStreamingEpoch = 0;

		EnqueueMeshChunk(BuildResult.ChunkCoord);
		MarkProxyGroupsForChunkChange(BuildResult.ChunkCoord);
		MarkAdjacentChunksForRemesh(BuildResult.ChunkCoord);
	}
}

void AVoxel::ProcessCompletedMeshBuilds()
{
	int32 ProcessedCount = 0;
	// Smooth chunk creation hitches by spreading game-thread mesh applies across more frames.
	const int32 FinalizationBudget = FMath::Clamp(GetChunkFinalizationBudget() / 2, 1, 3);
	FVoxelChunkMeshBuildResult BuildResult;
	while (ProcessedCount < FinalizationBudget && CompletedMeshResults.Dequeue(BuildResult))
	{
		if (const FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(BuildResult.ChunkCoord))
		{
			if (!RuntimeEntry->ChunkActor.IsValid()
				&& MeshSpawnFinalizationsThisTick >= MaxMeshSpawnFinalizationsPerTick)
			{
				CompletedMeshResults.Enqueue(MoveTemp(BuildResult));
				break;
			}
		}

		ActiveMeshRequestIds.Remove(BuildResult.RequestId);
		FinalizeChunkBuild(MoveTemp(BuildResult));
		++ProcessedCount;
	}
}

void AVoxel::ProcessCompletedProxyMeshBuilds()
{
	// Far-field HLOD can wait until nearby chunk streaming settles.
	if (GetActiveRealChunkJobCount() > 0 || HasPendingRealChunkWork())
	{
		return;
	}

	int32 ProcessedCount = 0;
	const int32 FinalizationBudget = 1;
	FVoxelProxyMeshBuildResult BuildResult;
	while (ProcessedCount < FinalizationBudget && CompletedProxyMeshResults.Dequeue(BuildResult))
	{
		ActiveProxyRequestIds.Remove(BuildResult.RequestId);
		FinalizeProxyGroupBuild(MoveTemp(BuildResult));
		++ProcessedCount;
	}
}

void AVoxel::RequestDeferredChunkRemesh(const FIntPoint& ChunkCoord)
{
	DeferredChunkRemeshCoords.Add(ChunkCoord);
}

void AVoxel::ProcessDeferredChunkRemeshes()
{
	const int32 Budget = FMath::Max(1, MaxDeferredChunkRemeshFlushesPerTick);
	TArray<FIntPoint> ToFlush;
	for (const FIntPoint& Coord : DeferredChunkRemeshCoords)
	{
		if (ToFlush.Num() >= Budget)
		{
			break;
		}
		ToFlush.Add(Coord);
	}

	for (const FIntPoint& Coord : ToFlush)
	{
		DeferredChunkRemeshCoords.Remove(Coord);
		MarkChunkForRemesh(Coord);
	}
}

void AVoxel::FinalizeChunkBuild(FVoxelChunkMeshBuildResult&& BuildResult)
{
	FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(BuildResult.ChunkCoord);
	if (RuntimeEntry == nullptr
		|| RuntimeEntry->State != EVoxelChunkRuntimeState::Meshing
		|| RuntimeEntry->ActiveRequestId != BuildResult.RequestId)
	{
		InFlightRequestIds.Remove(BuildResult.RequestId);
		++DroppedStaleMeshResultCount;
		++DroppedStaleChunkResultCount;
		return;
	}

	if (!RuntimeEntry->bIsDesired)
	{
		if (RuntimeEntry->ChunkActor.IsValid())
		{
			RuntimeEntry->ChunkActor->Destroy();
		}

		ChunkRuntimeEntries.Remove(BuildResult.ChunkCoord);
		InFlightRequestIds.Remove(BuildResult.RequestId);
		++DroppedStaleMeshResultCount;
		++DroppedStaleChunkResultCount;
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		if (RuntimeEntry->ChunkActor.IsValid())
		{
			RuntimeEntry->ChunkActor->Destroy();
		}

		ChunkRuntimeEntries.Remove(BuildResult.ChunkCoord);
		InFlightRequestIds.Remove(BuildResult.RequestId);
		++DroppedStaleMeshResultCount;
		++DroppedStaleChunkResultCount;
		return;
	}

	const double ApplyStartSeconds = FPlatformTime::Seconds();
	AVoxelChunk* ChunkActor = RuntimeEntry->ChunkActor.Get();
	const bool bWasRemesh = ChunkActor != nullptr;
	if (ChunkActor == nullptr)
	{
		ChunkActor = World->SpawnActor<AVoxelChunk>();
		if (ChunkActor != nullptr)
		{
			++MeshSpawnFinalizationsThisTick;
		}
		if (ChunkActor == nullptr)
		{
			InFlightRequestIds.Remove(BuildResult.RequestId);
			RuntimeEntry->ActiveRequestId = ReserveRequestId();
			InFlightRequestIds.Add(RuntimeEntry->ActiveRequestId);
			RuntimeEntry->State = EVoxelChunkRuntimeState::PendingMeshing;
			RuntimeEntry->bNeedsRemesh = true;
			EnqueueMeshChunk(BuildResult.ChunkCoord);
			GenerationStatusText = FString::Printf(TEXT("Status            Retrying mesh for chunk (%d, %d)"), BuildResult.ChunkCoord.X, BuildResult.ChunkCoord.Y);
			return;
		}
	}

	RuntimeEntry->CachedMeshData = MakeShared<FVoxelChunkMeshData, ESPMode::ThreadSafe>(MoveTemp(BuildResult.MeshData));
	const FVoxelChunkMeshData& CachedMeshData = *RuntimeEntry->CachedMeshData;

	const FVector ChunkWorldLocation = GetChunkWorldLocation(BuildResult.ChunkCoord);
	const FVector2D ChunkOffset = GetChunkOffset(BuildResult.ChunkCoord);
	const bool bShouldEnableChunkCollision = RuntimeEntry->bIsDesired && GetChunkRingDistance(BuildResult.ChunkCoord) <= CollisionChunkRadius;
	ChunkActor->Initialize(ChunkGridSize, VoxelSize, ChunkWorldLocation, ChunkOffset, ChunkHeight);
	ChunkActor->SetChunkCollisionEnabled(bShouldEnableChunkCollision);
	ChunkActor->ApplyMeshData(CachedMeshData);
	ChunkActor->SetChunkRendered(RuntimeEntry->bIsRendered);

	RuntimeEntry->ChunkActor = ChunkActor;
	LastMeshBuildTime = BuildResult.BuildDuration;
	LastChunkApplyTime = FTimespan::FromSeconds(FPlatformTime::Seconds() - ApplyStartSeconds);
	++FinalizedChunksThisTick;
	if (bWasRemesh)
	{
		++RemeshedChunkCount;
	}
	else
	{
		++FirstChunkBuildCount;
	}

	InFlightRequestIds.Remove(BuildResult.RequestId);
	if (RuntimeEntry->bNeedsRemesh)
	{
		RuntimeEntry->ActiveRequestId = ReserveRequestId();
		InFlightRequestIds.Add(RuntimeEntry->ActiveRequestId);
		RuntimeEntry->State = EVoxelChunkRuntimeState::PendingMeshing;
		EnqueueMeshChunk(BuildResult.ChunkCoord);
		GenerationStatusText = FString::Printf(TEXT("Status            Remeshing chunk (%d, %d)"), BuildResult.ChunkCoord.X, BuildResult.ChunkCoord.Y);
		return;
	}

	RuntimeEntry->State = EVoxelChunkRuntimeState::Ready;
	RuntimeEntry->ActiveRequestId = 0;
	RuntimeEntry->ActiveRequestStreamingEpoch = 0;
	GenerationStatusText = FString::Printf(TEXT("Status            Finalized chunk (%d, %d)"), BuildResult.ChunkCoord.X, BuildResult.ChunkCoord.Y);
}

void AVoxel::FinalizeProxyGroupBuild(FVoxelProxyMeshBuildResult&& BuildResult)
{
	FVoxelProxyGroupRuntimeEntry* RuntimeEntry = ProxyGroupRuntimeEntries.Find(BuildResult.GroupCoord);
	if (RuntimeEntry == nullptr
		|| RuntimeEntry->State != EVoxelProxyGroupRuntimeState::Meshing
		|| RuntimeEntry->ActiveRequestId != BuildResult.RequestId
		|| RuntimeEntry->ActiveBuildRevision != BuildResult.Revision)
	{
		++DroppedStaleMeshResultCount;
		++DroppedStaleChunkResultCount;
		return;
	}

	int32 MinDistance = 0;
	int32 MaxDistance = 0;
	GetProxyGroupDistanceRange(BuildResult.GroupCoord, MinDistance, MaxDistance);
	const bool bOutdatedForMovement = BuildResult.RequestStreamingEpoch < RuntimeEntry->LastRequestedStreamingEpoch
		&& MinDistance > ProxyRealChunkRadius;
	if (bOutdatedForMovement)
	{
		RuntimeEntry->ActiveBuildRevision = 0;
		RuntimeEntry->ActiveRequestId = 0;
		RuntimeEntry->ActiveRequestStreamingEpoch = 0;
		RuntimeEntry->bNeedsRebuild = RuntimeEntry->bIsDesired;
		RuntimeEntry->State = RuntimeEntry->bIsDesired ? EVoxelProxyGroupRuntimeState::Queued : EVoxelProxyGroupRuntimeState::Ready;
		if (RuntimeEntry->bIsDesired)
		{
			EnqueueProxyGroup(BuildResult.GroupCoord);
		}
		else
		{
			UnloadProxyGroup(BuildResult.GroupCoord);
		}

		++DroppedOutdatedResultCount;
		++DroppedStaleMeshResultCount;
		++DroppedStaleChunkResultCount;
		return;
	}

	if (!RuntimeEntry->bIsDesired || RuntimeEntry->Revision != BuildResult.Revision)
	{
		RuntimeEntry->ActiveBuildRevision = 0;
		RuntimeEntry->ActiveRequestId = 0;
		RuntimeEntry->ActiveRequestStreamingEpoch = 0;
		RuntimeEntry->bNeedsRebuild = RuntimeEntry->bIsDesired;
		RuntimeEntry->State = RuntimeEntry->bIsDesired ? EVoxelProxyGroupRuntimeState::Queued : EVoxelProxyGroupRuntimeState::Ready;
		if (RuntimeEntry->bIsDesired)
		{
			EnqueueProxyGroup(BuildResult.GroupCoord);
		}
		else
		{
			UnloadProxyGroup(BuildResult.GroupCoord);
		}

		++DroppedStaleMeshResultCount;
		++DroppedStaleChunkResultCount;
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		if (RuntimeEntry->ProxyActor.IsValid())
		{
			RuntimeEntry->ProxyActor->Destroy();
		}

		UnloadProxyGroup(BuildResult.GroupCoord);
		++DroppedStaleMeshResultCount;
		++DroppedStaleChunkResultCount;
		return;
	}

	const double ApplyStartSeconds = FPlatformTime::Seconds();
	AVoxelProxyChunk* ProxyActor = RuntimeEntry->ProxyActor.Get();
	if (ProxyActor == nullptr)
	{
		ProxyActor = World->SpawnActor<AVoxelProxyChunk>();
		if (ProxyActor == nullptr)
		{
			RuntimeEntry->ActiveRequestId = 0;
			RuntimeEntry->State = EVoxelProxyGroupRuntimeState::Queued;
			RuntimeEntry->bNeedsRebuild = true;
			EnqueueProxyGroup(BuildResult.GroupCoord);
			GenerationStatusText = FString::Printf(TEXT("Status            Retrying proxy group (%d, %d)"), BuildResult.GroupCoord.X, BuildResult.GroupCoord.Y);
			return;
		}
	}

	ProxyActor->Initialize(GetProxyGroupWorldLocation(BuildResult.GroupCoord));
	ProxyActor->ApplyMeshData(BuildResult.MeshData);
	ProxyActor->SetProxyRendered(RuntimeEntry->bShouldRenderProxy);

	RuntimeEntry->ProxyActor = ProxyActor;
	RuntimeEntry->SourceChunkCount = BuildResult.SourceChunkCount;
	RuntimeEntry->ApproximateMeshBytes = BuildResult.ApproximateMeshBytes;
	LastMeshBuildTime = BuildResult.BuildDuration;
	LastChunkApplyTime = FTimespan::FromSeconds(FPlatformTime::Seconds() - ApplyStartSeconds);

	RuntimeEntry->ActiveBuildRevision = 0;
	RuntimeEntry->ActiveRequestId = 0;
	RuntimeEntry->ActiveRequestStreamingEpoch = 0;
	if (RuntimeEntry->bNeedsRebuild)
	{
		RuntimeEntry->State = EVoxelProxyGroupRuntimeState::Queued;
		EnqueueProxyGroup(BuildResult.GroupCoord);
		GenerationStatusText = FString::Printf(TEXT("Status            Rebuilding proxy group (%d, %d)"), BuildResult.GroupCoord.X, BuildResult.GroupCoord.Y);
		return;
	}

	RuntimeEntry->State = EVoxelProxyGroupRuntimeState::Ready;
	GenerationStatusText = FString::Printf(TEXT("Status            Finalized proxy group (%d, %d)"), BuildResult.GroupCoord.X, BuildResult.GroupCoord.Y);
}

bool AVoxel::TryPopNextQueuedVoxelChunk(FIntPoint& OutChunkCoord)
{
	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = TNumericLimits<int32>::Max();

	for (int32 QueueIndex = QueuedVoxelChunkCoords.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		const FIntPoint ChunkCoord = QueuedVoxelChunkCoords[QueueIndex];
		const FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
		if (RuntimeEntry == nullptr || RuntimeEntry->State != EVoxelChunkRuntimeState::Queued || !RuntimeEntry->bIsDesired)
		{
			continue;
		}

		const int32 EpochPenalty = static_cast<int32>(CurrentStreamingEpoch - RuntimeEntry->LastRequestedStreamingEpoch) * 100000;
		const int32 PriorityScore = GetChunkPriorityScore(ChunkCoord) + EpochPenalty;
		if (PriorityScore < BestPriority)
		{
			BestPriority = PriorityScore;
			BestIndex = QueueIndex;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return false;
	}

	OutChunkCoord = QueuedVoxelChunkCoords[BestIndex];
	if (FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(OutChunkCoord))
	{
		RuntimeEntry->bVoxelQueued = false;
	}
	QueuedVoxelChunkCoords.RemoveAtSwap(BestIndex);
	return true;
}

bool AVoxel::TryPopNextQueuedMeshChunk(FIntPoint& OutChunkCoord)
{
	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = TNumericLimits<int32>::Max();

	for (int32 QueueIndex = QueuedMeshChunkCoords.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		const FIntPoint ChunkCoord = QueuedMeshChunkCoords[QueueIndex];
		const FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
		if (RuntimeEntry == nullptr
			|| RuntimeEntry->State != EVoxelChunkRuntimeState::PendingMeshing
			|| !RuntimeEntry->bIsDesired
			|| !RuntimeEntry->CachedVoxels.IsValid()
			|| !RuntimeEntry->bNeedsRemesh)
		{
			continue;
		}

		const int32 EpochPenalty = static_cast<int32>(CurrentStreamingEpoch - RuntimeEntry->LastRequestedStreamingEpoch) * 100000;
		const int32 PriorityScore = GetChunkPriorityScore(ChunkCoord) + EpochPenalty;
		if (PriorityScore < BestPriority)
		{
			BestPriority = PriorityScore;
			BestIndex = QueueIndex;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return false;
	}

	OutChunkCoord = QueuedMeshChunkCoords[BestIndex];
	if (FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(OutChunkCoord))
	{
		RuntimeEntry->bMeshQueued = false;
	}
	QueuedMeshChunkCoords.RemoveAtSwap(BestIndex);
	return true;
}

bool AVoxel::TryPopNextQueuedProxyGroup(FIntPoint& OutGroupCoord)
{
	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = TNumericLimits<int32>::Max();

	for (int32 QueueIndex = QueuedProxyGroupCoords.Num() - 1; QueueIndex >= 0; --QueueIndex)
	{
		const FIntPoint GroupCoord = QueuedProxyGroupCoords[QueueIndex];
		const FVoxelProxyGroupRuntimeEntry* RuntimeEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
		if (RuntimeEntry == nullptr
			|| RuntimeEntry->State != EVoxelProxyGroupRuntimeState::Queued
			|| !RuntimeEntry->bIsDesired
			|| !RuntimeEntry->bNeedsRebuild
			|| RuntimeEntry->ReadySourceChunkCount <= 0)
		{
			continue;
		}

		const int32 EpochPenalty = static_cast<int32>(CurrentStreamingEpoch - RuntimeEntry->LastRequestedStreamingEpoch) * 100000;
		const int32 PriorityScore = GetProxyGroupPriorityScore(GroupCoord) + EpochPenalty;
		if (PriorityScore < BestPriority)
		{
			BestPriority = PriorityScore;
			BestIndex = QueueIndex;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return false;
	}

	OutGroupCoord = QueuedProxyGroupCoords[BestIndex];
	if (FVoxelProxyGroupRuntimeEntry* RuntimeEntry = ProxyGroupRuntimeEntries.Find(OutGroupCoord))
	{
		RuntimeEntry->bProxyQueued = false;
	}
	QueuedProxyGroupCoords.RemoveAtSwap(BestIndex);
	return true;
}

void AVoxel::UnloadChunk(const FIntPoint& ChunkCoord)
{
	FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
	if (RuntimeEntry != nullptr && RuntimeEntry->ChunkActor.IsValid())
	{
		RuntimeEntry->ChunkActor->Destroy();
	}

	if (RuntimeEntry != nullptr && RuntimeEntry->ActiveRequestId != 0)
	{
		InFlightRequestIds.Remove(RuntimeEntry->ActiveRequestId);
		ActiveVoxelRequestIds.Remove(RuntimeEntry->ActiveRequestId);
		ActiveMeshRequestIds.Remove(RuntimeEntry->ActiveRequestId);
	}

	ChunkRuntimeEntries.Remove(ChunkCoord);
	for (const FIntPoint NeighborOffset : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
	{
		RequestDeferredChunkRemesh(ChunkCoord + NeighborOffset);
	}
	MarkProxyGroupsForChunkChange(ChunkCoord);
}

void AVoxel::UnloadProxyGroup(const FIntPoint& GroupCoord)
{
	FVoxelProxyGroupRuntimeEntry* RuntimeEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
	if (RuntimeEntry != nullptr && RuntimeEntry->ProxyActor.IsValid())
	{
		RuntimeEntry->ProxyActor->Destroy();
	}

	if (RuntimeEntry != nullptr && RuntimeEntry->ActiveRequestId != 0)
	{
		ActiveProxyRequestIds.Remove(RuntimeEntry->ActiveRequestId);
	}

	ProxyGroupRuntimeEntries.Remove(GroupCoord);
}

void AVoxel::RefreshChunkStats()
{
	GeneratedChunkCount = 0;
	VisibleLoadedChunkCount = 0;
	HiddenLoadedChunkCount = 0;
	QueuedChunkCount = 0;
	VisibleQueuedChunkCount = 0;
	GeneratingChunkCount = 0;
	ActiveProxyGroupCount = 0;
	QueuedProxyGroupCount = 0;
	BuildingProxyGroupCount = 0;
	VisibleProxyGroupCount = 0;
	ActiveRealChunkJobCountStat = GetActiveRealChunkJobCount();
	ActiveProxyJobCountStat = GetActiveProxyJobCount();
	ProxyMeshBytes = 0;
	TotalSolidVoxels = 0;
	TotalSurfaceVoxels = 0;
	TotalCulledInteriorVoxels = 0;
	TotalVisibleFaces = 0;
	TotalTriangles = 0;
	TotalVertices = 0;
	FVoxelTerrainSampleStats AggregatedTerrainStats;

	for (const TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		switch (Entry.Value.State)
		{
		case EVoxelChunkRuntimeState::Queued:
		case EVoxelChunkRuntimeState::PendingMeshing:
			++QueuedChunkCount;
			if (Entry.Value.bIsInView)
			{
				++VisibleQueuedChunkCount;
			}
			break;

		case EVoxelChunkRuntimeState::GeneratingVoxels:
		case EVoxelChunkRuntimeState::Meshing:
			++GeneratingChunkCount;
			if (Entry.Value.bIsInView)
			{
				++VisibleQueuedChunkCount;
			}
			break;

		case EVoxelChunkRuntimeState::Ready:
			break;
		}

		if (Entry.Value.ChunkActor.IsValid())
		{
			++GeneratedChunkCount;
			if (Entry.Value.bIsRendered)
			{
				++VisibleLoadedChunkCount;
			}
			else
			{
				++HiddenLoadedChunkCount;
			}
			const FVoxelChunkMeshStats& MeshStats = Entry.Value.ChunkActor->GetMeshStats();
			TotalSolidVoxels += MeshStats.SolidVoxelCount;
			TotalSurfaceVoxels += MeshStats.SurfaceVoxelCount;
			TotalCulledInteriorVoxels += MeshStats.CulledInteriorVoxelCount;
			TotalVisibleFaces += MeshStats.FaceCount;
			TotalTriangles += MeshStats.TriangleCount;
			TotalVertices += MeshStats.VertexCount;
		}

		if (Entry.Value.bIsDesired && Entry.Value.TerrainStats.HasSamples())
		{
			AggregatedTerrainStats.Merge(Entry.Value.TerrainStats);
		}
	}

	for (const TPair<FIntPoint, FVoxelProxyGroupRuntimeEntry>& Entry : ProxyGroupRuntimeEntries)
	{
		switch (Entry.Value.State)
		{
		case EVoxelProxyGroupRuntimeState::Queued:
			++QueuedProxyGroupCount;
			break;

		case EVoxelProxyGroupRuntimeState::Meshing:
			++BuildingProxyGroupCount;
			break;

		case EVoxelProxyGroupRuntimeState::Ready:
			break;
		}

		if (Entry.Value.ProxyActor.IsValid())
		{
			++ActiveProxyGroupCount;
			if (Entry.Value.bIsRendered)
			{
				++VisibleProxyGroupCount;
			}
		}

		ProxyMeshBytes += Entry.Value.ApproximateMeshBytes;
	}

	TotalVoxelSlots = GetVoxelSlotsPerChunk(ChunkGridSize, ChunkHeight) * GeneratedChunkCount;

	if (AggregatedTerrainStats.HasSamples())
	{
		MinSampledTerrainHeight = AggregatedTerrainStats.MinHeight;
		MaxSampledTerrainHeight = AggregatedTerrainStats.MaxHeight;
		AverageSampledTerrainHeight = static_cast<float>(AggregatedTerrainStats.GetAverageHeight());
	}
	else
	{
		MinSampledTerrainHeight = 0;
		MaxSampledTerrainHeight = 0;
		AverageSampledTerrainHeight = 0.0f;
	}
}

void AVoxel::MarkChunkForRemesh(const FIntPoint& ChunkCoord)
{
	FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
	if (RuntimeEntry == nullptr || !RuntimeEntry->bIsDesired || !RuntimeEntry->CachedVoxels.IsValid())
	{
		return;
	}

	if (RuntimeEntry->State == EVoxelChunkRuntimeState::Queued || RuntimeEntry->State == EVoxelChunkRuntimeState::GeneratingVoxels)
	{
		return;
	}

	RuntimeEntry->bNeedsRemesh = true;
	RuntimeEntry->CachedMeshData.Reset();
	MarkProxyGroupsForChunkChange(ChunkCoord);
	if (RuntimeEntry->State == EVoxelChunkRuntimeState::Meshing)
	{
		return;
	}

	if (RuntimeEntry->ActiveRequestId == 0 || !InFlightRequestIds.Contains(RuntimeEntry->ActiveRequestId))
	{
		RuntimeEntry->ActiveRequestId = ReserveRequestId();
		InFlightRequestIds.Add(RuntimeEntry->ActiveRequestId);
	}

	RuntimeEntry->LastRequestedStreamingEpoch = CurrentStreamingEpoch;
	RuntimeEntry->State = EVoxelChunkRuntimeState::PendingMeshing;
	EnqueueMeshChunk(ChunkCoord);
}

void AVoxel::MarkAdjacentChunksForRemesh(const FIntPoint& ChunkCoord)
{
	MarkChunkForRemesh(ChunkCoord + FIntPoint(1, 0));
	MarkChunkForRemesh(ChunkCoord + FIntPoint(-1, 0));
	MarkChunkForRemesh(ChunkCoord + FIntPoint(0, 1));
	MarkChunkForRemesh(ChunkCoord + FIntPoint(0, -1));
}

void AVoxel::MarkProxyGroupForRebuild(const FIntPoint& GroupCoord)
{
	FVoxelProxyGroupRuntimeEntry* RuntimeEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
	if (RuntimeEntry == nullptr)
	{
		FVoxelProxyGroupRuntimeEntry& NewEntry = ProxyGroupRuntimeEntries.Add(GroupCoord);
		NewEntry.State = EVoxelProxyGroupRuntimeState::Queued;
		NewEntry.LastRequestedStreamingEpoch = CurrentStreamingEpoch;
		NewEntry.Revision = 1;
		NewEntry.bNeedsRebuild = true;
		NewEntry.ReadySourceChunkCount = GetReadySourceChunkCountForProxyGroup(GroupCoord);
		if (NewEntry.ReadySourceChunkCount > 0
			&& NewEntry.ReadySourceChunkCount >= GetDesiredSourceChunkCountForProxyGroup(GroupCoord))
		{
			EnqueueProxyGroup(GroupCoord);
		}
		return;
	}

	++RuntimeEntry->Revision;
	RuntimeEntry->LastRequestedStreamingEpoch = CurrentStreamingEpoch;
	RuntimeEntry->bNeedsRebuild = true;
	RuntimeEntry->ReadySourceChunkCount = GetReadySourceChunkCountForProxyGroup(GroupCoord);
	if (RuntimeEntry->State != EVoxelProxyGroupRuntimeState::Meshing
		&& RuntimeEntry->ReadySourceChunkCount > 0
		&& RuntimeEntry->ReadySourceChunkCount >= GetDesiredSourceChunkCountForProxyGroup(GroupCoord))
	{
		RuntimeEntry->State = EVoxelProxyGroupRuntimeState::Queued;
		RuntimeEntry->ActiveRequestId = 0;
		EnqueueProxyGroup(GroupCoord);
	}
}

void AVoxel::MarkProxyGroupsForChunkChange(const FIntPoint& ChunkCoord)
{
	TSet<FIntPoint> GroupsToRebuild;
	GroupsToRebuild.Add(GetProxyGroupCoordFromChunkCoord(ChunkCoord));
	GroupsToRebuild.Add(GetProxyGroupCoordFromChunkCoord(ChunkCoord + FIntPoint(1, 0)));
	GroupsToRebuild.Add(GetProxyGroupCoordFromChunkCoord(ChunkCoord + FIntPoint(-1, 0)));
	GroupsToRebuild.Add(GetProxyGroupCoordFromChunkCoord(ChunkCoord + FIntPoint(0, 1)));
	GroupsToRebuild.Add(GetProxyGroupCoordFromChunkCoord(ChunkCoord + FIntPoint(0, -1)));

	for (const FIntPoint& GroupCoord : GroupsToRebuild)
	{
		MarkProxyGroupForRebuild(GroupCoord);
	}
}

void AVoxel::UpdateProxyGroupDesiredStates(double RequestTimeSeconds)
{
	TSet<FIntPoint> DesiredGroupCoords;
	for (const TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		if (!Entry.Value.bIsDesired)
		{
			continue;
		}

		const FIntPoint GroupCoord = GetProxyGroupCoordFromChunkCoord(Entry.Key);
		if (DesiredGroupCoords.Contains(GroupCoord))
		{
			continue;
		}

		DesiredGroupCoords.Add(GroupCoord);
		QueueProxyGroupRequest(GroupCoord, RequestTimeSeconds);
	}

	TArray<FIntPoint> GroupCoordsToRemove;
	for (TPair<FIntPoint, FVoxelProxyGroupRuntimeEntry>& Entry : ProxyGroupRuntimeEntries)
	{
		Entry.Value.ReadySourceChunkCount = GetReadySourceChunkCountForProxyGroup(Entry.Key);
		const int32 DesiredSourceChunkCount = GetDesiredSourceChunkCountForProxyGroup(Entry.Key);
		if (!Entry.Value.bIsDesired)
		{
			if (Entry.Value.State == EVoxelProxyGroupRuntimeState::Ready || Entry.Value.State == EVoxelProxyGroupRuntimeState::Queued)
			{
				GroupCoordsToRemove.Add(Entry.Key);
			}

			continue;
		}

		if (Entry.Value.State != EVoxelProxyGroupRuntimeState::Meshing
			&& Entry.Value.ReadySourceChunkCount > 0
			&& Entry.Value.ReadySourceChunkCount >= DesiredSourceChunkCount
			&& (Entry.Value.bNeedsRebuild || (Entry.Value.State == EVoxelProxyGroupRuntimeState::Ready && !Entry.Value.ProxyActor.IsValid())))
		{
			Entry.Value.State = EVoxelProxyGroupRuntimeState::Queued;
			Entry.Value.ActiveRequestId = 0;
			EnqueueProxyGroup(Entry.Key);
		}
	}

	for (const FIntPoint& GroupCoord : GroupCoordsToRemove)
	{
		UnloadProxyGroup(GroupCoord);
	}
}

FVoxelChunkNeighborBorderData AVoxel::GetNeighborBorderDataForChunk(const FIntPoint& ChunkCoord) const
{
	FVoxelChunkNeighborBorderData NeighborBorderData;

	const auto TryAssignNeighbor = [this](const FIntPoint& NeighborCoord, TOptional<FVoxelChunkBorderData>& OutBorderData)
	{
		if (const FVoxelChunkRuntimeEntry* NeighborEntry = ChunkRuntimeEntries.Find(NeighborCoord))
		{
			if (NeighborEntry->BorderData.HasData())
			{
				OutBorderData = NeighborEntry->BorderData;
			}
		}
	};

	TryAssignNeighbor(ChunkCoord + FIntPoint(1, 0), NeighborBorderData.PositiveX);
	TryAssignNeighbor(ChunkCoord + FIntPoint(-1, 0), NeighborBorderData.NegativeX);
	TryAssignNeighbor(ChunkCoord + FIntPoint(0, 1), NeighborBorderData.PositiveY);
	TryAssignNeighbor(ChunkCoord + FIntPoint(0, -1), NeighborBorderData.NegativeY);
	return NeighborBorderData;
}

int32 AVoxel::GetActiveAsyncJobCount() const
{
	return ActiveVoxelRequestIds.Num() + ActiveMeshRequestIds.Num() + ActiveProxyRequestIds.Num();
}

int32 AVoxel::GetActiveRealChunkJobCount() const
{
	return ActiveVoxelRequestIds.Num() + ActiveMeshRequestIds.Num();
}

int32 AVoxel::GetActiveProxyJobCount() const
{
	return ActiveProxyRequestIds.Num();
}

bool AVoxel::ShouldRefreshStreamingChunks() const
{
	if (!bHasStreamedChunkCoord)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	return (World->GetTimeSeconds() - LastStreamingRefreshTimeSeconds) >= static_cast<double>(StreamingUpdateInterval);
}

bool AVoxel::ShouldRefreshStats() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	return (World->GetTimeSeconds() - LastStatsRefreshTimeSeconds) >= static_cast<double>(StatsRefreshInterval);
}

int32 AVoxel::GetGenerationJobLimit() const
{
	if (!bAutoScaleStreamingBudgets)
	{
		return MaxConcurrentGenerationJobs;
	}

	const int32 WorkerThreadCount = GetAutoScaledWorkerThreadCount();
	return FMath::Clamp(FMath::Max(2, WorkerThreadCount - 1), 2, 12);
}

int32 AVoxel::GetQueuedJobStartBudget() const
{
	if (!bAutoScaleStreamingBudgets)
	{
		return MaxQueuedJobStartsPerTick;
	}

	return FMath::Clamp((GetGenerationJobLimit() + 1) / 2, 2, 8);
}

int32 AVoxel::GetChunkFinalizationBudget() const
{
	if (!bAutoScaleStreamingBudgets)
	{
		return MaxChunkFinalizationsPerTick;
	}

	return FMath::Clamp((GetGenerationJobLimit() + 1) / 2, 2, 6);
}

int32 AVoxel::GetProxyJobLimit() const
{
	if (!bAutoScaleStreamingBudgets)
	{
		return MaxConcurrentProxyJobs;
	}

	return FMath::Clamp(GetGenerationJobLimit() / 4, 1, 2);
}

bool AVoxel::HasPendingRealChunkWork() const
{
	for (const TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		if (!Entry.Value.bIsDesired)
		{
			continue;
		}

		if (Entry.Value.State == EVoxelChunkRuntimeState::Queued
			|| Entry.Value.State == EVoxelChunkRuntimeState::PendingMeshing
			|| Entry.Value.bVoxelQueued
			|| Entry.Value.bMeshQueued
			|| Entry.Value.bNeedsRemesh)
		{
			return true;
		}
	}

	return false;
}

void AVoxel::UpdateChunkVisibilityStates()
{
	const UWorld* World = GetWorld();
	const double CurrentTimeSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;
	const float DeltaTime = World != nullptr ? World->GetDeltaSeconds() : 0.0f;

	FConvexVolume ViewFrustum;
	const bool bHasViewFrustum = TryBuildStreamingViewFrustum(ViewFrustum);
	const FVector ExtentInflation = ComputeChunkViewExtentInflation(DeltaTime);
	const float RenderHorizontalMult = FMath::Max(1.0f, ChunkViewRenderHorizontalExtentMultiplier);

	for (TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		const bool bCanBeVisible = Entry.Value.bIsDesired;
		const bool bStrictInView = bCanBeVisible && (!bHasViewFrustum || IsChunkInView(Entry.Key, ViewFrustum));
		const bool bRenderFrustumHit = bCanBeVisible
			&& (!bHasViewFrustum || IsChunkAabbIntersectingFrustum(Entry.Key, ViewFrustum, RenderHorizontalMult, ExtentInflation));

		Entry.Value.bIsInView = bStrictInView;
		Entry.Value.bIsInRenderFrustum = bRenderFrustumHit;
		if (bRenderFrustumHit)
		{
			Entry.Value.LastVisibleTimeSeconds = CurrentTimeSeconds;
		}
	}
}

void AVoxel::UpdateRenderArbitration()
{
	const UWorld* World = GetWorld();
	const double CurrentTimeSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;
	const double GracePeriodSeconds = static_cast<double>(ViewCullingGracePeriod);
	const int32 EffectiveProxyOnlyStartRadius = FMath::Max(ProxyOnlyStartRadius, ProxyRealChunkRadius);
	const int32 MaxCollisionStateChangesPerTick = 2;
	int32 CollisionToggleCount = 0;

	for (TPair<FIntPoint, FVoxelProxyGroupRuntimeEntry>& Entry : ProxyGroupRuntimeEntries)
	{
		int32 MinDistance = 0;
		int32 MaxDistance = 0;
		GetProxyGroupDistanceRange(Entry.Key, MinDistance, MaxDistance);

		const bool bHasRenderableProxy = Entry.Value.State == EVoxelProxyGroupRuntimeState::Ready && Entry.Value.ProxyActor.IsValid();
		const bool bHasCompleteCoverage = Entry.Value.ReadySourceChunkCount > 0
			&& Entry.Value.ReadySourceChunkCount >= GetDesiredSourceChunkCountForProxyGroup(Entry.Key);
		const bool bShouldRenderProxy = Entry.Value.bIsDesired
			&& bHasRenderableProxy
			&& bHasCompleteCoverage
			&& MaxDistance > ProxyRealChunkRadius
			&& MinDistance > ProxyRealChunkRadius;
		Entry.Value.bShouldRenderProxy = bShouldRenderProxy;

		if (Entry.Value.bIsRendered != bShouldRenderProxy)
		{
			Entry.Value.bIsRendered = bShouldRenderProxy;
			++ProxyTransitionsThisTick;
			if (AVoxelProxyChunk* ProxyActor = Entry.Value.ProxyActor.Get())
			{
				ProxyActor->SetProxyRendered(bShouldRenderProxy);
			}
		}
	}

	TArray<FIntPoint> PendingRenderShows;
	TArray<FIntPoint> PendingRenderHides;
	PendingRenderShows.Reserve(32);
	PendingRenderHides.Reserve(32);

	for (TPair<FIntPoint, FVoxelChunkRuntimeEntry>& Entry : ChunkRuntimeEntries)
	{
		const bool bWithinGracePeriod = Entry.Value.bIsDesired
			&& GracePeriodSeconds > 0.0
			&& (CurrentTimeSeconds - Entry.Value.LastVisibleTimeSeconds) <= GracePeriodSeconds;
		const FIntPoint GroupCoord = GetProxyGroupCoordFromChunkCoord(Entry.Key);
		const FVoxelProxyGroupRuntimeEntry* ProxyGroupEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
		const int32 ChunkDistance = GetChunkRingDistance(Entry.Key);
		const bool bForceRenderNearbyChunk = ChunkDistance <= EffectiveProxyOnlyStartRadius;
		const bool bHideForProxy = ProxyGroupEntry != nullptr && ProxyGroupEntry->bShouldRenderProxy;
		const bool bShouldRenderChunk = Entry.Value.bIsDesired
			&& !bHideForProxy
			&& (bForceRenderNearbyChunk || Entry.Value.bIsInRenderFrustum || bWithinGracePeriod);
		const bool bShouldEnableChunkCollision = Entry.Value.bIsDesired && ChunkDistance <= CollisionChunkRadius;
		if (AVoxelChunk* ChunkActor = Entry.Value.ChunkActor.Get())
		{
			if (CollisionToggleCount < MaxCollisionStateChangesPerTick
				&& ChunkActor->IsChunkCollisionEnabled() != bShouldEnableChunkCollision)
			{
				ChunkActor->SetChunkCollisionEnabled(bShouldEnableChunkCollision, Entry.Value.CachedMeshData.Get());
				++CollisionToggleCount;
			}
		}

		if (!Entry.Value.ChunkActor.IsValid())
		{
			continue;
		}

		if (Entry.Value.bIsRendered != bShouldRenderChunk)
		{
			if (bShouldRenderChunk)
			{
				PendingRenderShows.Add(Entry.Key);
			}
			else
			{
				PendingRenderHides.Add(Entry.Key);
			}
		}
	}

	int32 ToggleBudget = MaxChunkRenderVisibilityTogglesPerTick;
	const bool bUnlimitedToggles = ToggleBudget <= 0;

	auto ApplyChunkRenderState = [this](const FIntPoint& ChunkCoord, const bool bShouldRender)
	{
		FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
		if (RuntimeEntry == nullptr || !RuntimeEntry->ChunkActor.IsValid())
		{
			return;
		}

		if (RuntimeEntry->bIsRendered == bShouldRender)
		{
			return;
		}

		RuntimeEntry->bIsRendered = bShouldRender;
		++ChunkRenderVisibilityTransitionsThisTick;
		RuntimeEntry->ChunkActor->SetChunkRendered(bShouldRender);
	};

	for (const FIntPoint& ChunkCoord : PendingRenderShows)
	{
		if (!bUnlimitedToggles && ToggleBudget <= 0)
		{
			break;
		}

		const FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
		if (RuntimeEntry == nullptr)
		{
			continue;
		}

		const bool bWithinGracePeriod = RuntimeEntry->bIsDesired
			&& GracePeriodSeconds > 0.0
			&& (CurrentTimeSeconds - RuntimeEntry->LastVisibleTimeSeconds) <= GracePeriodSeconds;
		const FIntPoint GroupCoord = GetProxyGroupCoordFromChunkCoord(ChunkCoord);
		const FVoxelProxyGroupRuntimeEntry* ProxyGroupEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
		const int32 ChunkDistance = GetChunkRingDistance(ChunkCoord);
		const bool bForceRenderNearbyChunk = ChunkDistance <= EffectiveProxyOnlyStartRadius;
		const bool bHideForProxy = ProxyGroupEntry != nullptr && ProxyGroupEntry->bShouldRenderProxy;
		const bool bShouldRenderChunk = RuntimeEntry->bIsDesired
			&& !bHideForProxy
			&& (bForceRenderNearbyChunk || RuntimeEntry->bIsInRenderFrustum || bWithinGracePeriod);
		if (!bShouldRenderChunk)
		{
			continue;
		}

		ApplyChunkRenderState(ChunkCoord, true);
		if (!bUnlimitedToggles)
		{
			--ToggleBudget;
		}
	}

	for (const FIntPoint& ChunkCoord : PendingRenderHides)
	{
		if (!bUnlimitedToggles && ToggleBudget <= 0)
		{
			break;
		}

		const FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
		if (RuntimeEntry == nullptr)
		{
			continue;
		}

		const bool bWithinGracePeriod = RuntimeEntry->bIsDesired
			&& GracePeriodSeconds > 0.0
			&& (CurrentTimeSeconds - RuntimeEntry->LastVisibleTimeSeconds) <= GracePeriodSeconds;
		const FIntPoint GroupCoord = GetProxyGroupCoordFromChunkCoord(ChunkCoord);
		const FVoxelProxyGroupRuntimeEntry* ProxyGroupEntry = ProxyGroupRuntimeEntries.Find(GroupCoord);
		const int32 ChunkDistance = GetChunkRingDistance(ChunkCoord);
		const bool bForceRenderNearbyChunk = ChunkDistance <= EffectiveProxyOnlyStartRadius;
		const bool bHideForProxy = ProxyGroupEntry != nullptr && ProxyGroupEntry->bShouldRenderProxy;
		const bool bShouldRenderChunk = RuntimeEntry->bIsDesired
			&& !bHideForProxy
			&& (bForceRenderNearbyChunk || RuntimeEntry->bIsInRenderFrustum || bWithinGracePeriod);
		if (bShouldRenderChunk)
		{
			continue;
		}

		ApplyChunkRenderState(ChunkCoord, false);
		if (!bUnlimitedToggles)
		{
			--ToggleBudget;
		}
	}
}

bool AVoxel::TryGetStreamingFocusLocation(FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		return false;
	}

	if (PlayerController->PlayerCameraManager != nullptr)
	{
		OutLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
		return true;
	}

	if (APawn* PlayerPawn = PlayerController->GetPawn())
	{
		OutLocation = PlayerPawn->GetActorLocation();
		return true;
	}

	return false;
}

bool AVoxel::TryBuildStreamingViewFrustum(FConvexVolume& OutViewFrustum) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (PlayerController == nullptr)
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(PlayerController->Player);
	if (LocalPlayer == nullptr || LocalPlayer->ViewportClient == nullptr || LocalPlayer->ViewportClient->Viewport == nullptr)
	{
		return false;
	}

	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData, INDEX_NONE))
	{
		return false;
	}

	GetViewFrustumBounds(OutViewFrustum, ProjectionData.ComputeViewProjectionMatrix(), false);
	return true;
}

FVector AVoxel::ComputeChunkViewExtentInflation(float DeltaTime)
{
	float UniformPad = FMath::Max(0.0f, ChunkViewRenderExtentPaddingCm);

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController != nullptr)
	{
		if (PlayerController->PlayerCameraManager != nullptr)
		{
			const FVector Forward = PlayerController->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal();
			if (bHasLastViewBiasSample && DeltaTime > KINDA_SMALL_NUMBER)
			{
				const float Dot = FMath::Clamp(FVector::DotProduct(Forward, LastViewBiasCameraForward), -1.0f, 1.0f);
				const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));
				const float AngularSpeedDegPerSec = AngleDeg / DeltaTime;
				UniformPad += AngularSpeedDegPerSec * ChunkViewRenderAngularSpeedPaddingScale;
			}

			LastViewBiasCameraForward = Forward;
			bHasLastViewBiasSample = true;
		}

		if (APawn* PlayerPawn = PlayerController->GetPawn())
		{
			const float LinearSpeed = PlayerPawn->GetVelocity().Size();
			UniformPad += LinearSpeed * ChunkViewRenderLinearSpeedPaddingScale;
		}
	}

	return FVector(UniformPad, UniformPad, UniformPad);
}

bool AVoxel::IsChunkAabbIntersectingFrustum(const FIntPoint& ChunkCoord, const FConvexVolume& ViewFrustum, float HorizontalExtentMultiplier, const FVector& ExtentInflation) const
{
	const float ChunkWorldSize = GetChunkWorldSize();
	const float HalfZ = (static_cast<float>(ChunkHeight) * VoxelSize) * 0.5f;
	const float HalfXY = ChunkWorldSize * 0.5f * HorizontalExtentMultiplier;
	FVector ChunkExtent(HalfXY, HalfXY, HalfZ);
	ChunkExtent += ExtentInflation;
	const FVector ChunkCenter = GetChunkWorldLocation(ChunkCoord) + FVector(ChunkWorldSize * 0.5f, ChunkWorldSize * 0.5f, HalfZ);
	return ViewFrustum.IntersectBox(ChunkCenter, ChunkExtent);
}

bool AVoxel::IsChunkInView(const FIntPoint& ChunkCoord, const FConvexVolume& ViewFrustum) const
{
	return IsChunkAabbIntersectingFrustum(ChunkCoord, ViewFrustum, 1.0f, FVector::ZeroVector);
}

float AVoxel::GetChunkWorldSize() const
{
	return static_cast<float>(ChunkGridSize) * VoxelSize * 2.0f;
}

int32 AVoxel::GetEffectiveUnloadRadius() const
{
	return FMath::Max(UnloadRadius, LoadRadius + 1);
}

int32 AVoxel::GetChunkPriorityScore(const FIntPoint& ChunkCoord) const
{
	if (!bHasStreamedChunkCoord)
	{
		return 0;
	}

	const int32 DeltaX = ChunkCoord.X - LastStreamedChunkCoord.X;
	const int32 DeltaY = ChunkCoord.Y - LastStreamedChunkCoord.Y;
	const int32 DistanceScore = (DeltaX * DeltaX) + (DeltaY * DeltaY);
	const FVoxelChunkRuntimeEntry* RuntimeEntry = ChunkRuntimeEntries.Find(ChunkCoord);
	const int32 VisibilityPenalty = (RuntimeEntry != nullptr && RuntimeEntry->bIsInView)
		? 0
		: FMath::Max(0, OutOfViewChunkJobPriorityPenalty);
	return VisibilityPenalty + DistanceScore;
}

int32 AVoxel::GetProxyGroupPriorityScore(const FIntPoint& GroupCoord) const
{
	int32 MinDistance = 0;
	int32 MaxDistance = 0;
	GetProxyGroupDistanceRange(GroupCoord, MinDistance, MaxDistance);
	return (MinDistance * MinDistance) + MaxDistance;
}

int32 AVoxel::GetDesiredSourceChunkCountForProxyGroup(const FIntPoint& GroupCoord) const
{
	int32 DesiredSourceChunkCount = 0;
	const FIntPoint GroupOriginChunkCoord = GetProxyGroupOriginChunkCoord(GroupCoord);
	for (int32 LocalX = 0; LocalX < FMath::Max(1, ProxyGroupChunkSpan); ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < FMath::Max(1, ProxyGroupChunkSpan); ++LocalY)
		{
			const FIntPoint ChunkCoord(GroupOriginChunkCoord.X + LocalX, GroupOriginChunkCoord.Y + LocalY);
			const FVoxelChunkRuntimeEntry* ChunkEntry = ChunkRuntimeEntries.Find(ChunkCoord);
			if (ChunkEntry == nullptr || !ChunkEntry->bIsDesired)
			{
				continue;
			}

			++DesiredSourceChunkCount;
		}
	}

	return DesiredSourceChunkCount;
}

uint64 AVoxel::ReserveRequestId()
{
	return NextChunkRequestId++;
}

FIntPoint AVoxel::GetChunkCoordFromWorldLocation(const FVector& WorldLocation) const
{
	const float ChunkWorldSize = GetChunkWorldSize();
	if (ChunkWorldSize <= 0.0f)
	{
		return FIntPoint::ZeroValue;
	}

	return FIntPoint(
		FMath::FloorToInt(WorldLocation.X / ChunkWorldSize),
		FMath::FloorToInt(WorldLocation.Y / ChunkWorldSize));
}

FIntPoint AVoxel::GetProxyGroupCoordFromChunkCoord(const FIntPoint& ChunkCoord) const
{
	const int32 GroupSpan = FMath::Max(1, ProxyGroupChunkSpan);
	return FIntPoint(
		FloorDivide(ChunkCoord.X, GroupSpan),
		FloorDivide(ChunkCoord.Y, GroupSpan));
}

FIntPoint AVoxel::GetProxyGroupOriginChunkCoord(const FIntPoint& GroupCoord) const
{
	const int32 GroupSpan = FMath::Max(1, ProxyGroupChunkSpan);
	return FIntPoint(GroupCoord.X * GroupSpan, GroupCoord.Y * GroupSpan);
}

FVector AVoxel::GetChunkWorldLocation(const FIntPoint& ChunkCoord) const
{
	const float ChunkWorldSize = GetChunkWorldSize();
	return FVector(ChunkCoord.X * ChunkWorldSize, ChunkCoord.Y * ChunkWorldSize, 0.0f);
}

FVector AVoxel::GetProxyGroupWorldLocation(const FIntPoint& GroupCoord) const
{
	return GetChunkWorldLocation(GetProxyGroupOriginChunkCoord(GroupCoord));
}

FVector2D AVoxel::GetChunkOffset(const FIntPoint& ChunkCoord) const
{
	return FVector2D(
		static_cast<float>(ChunkCoord.X * ChunkGridSize),
		static_cast<float>(ChunkCoord.Y * ChunkGridSize));
}

int32 AVoxel::GetChunkRingDistance(const FIntPoint& ChunkCoord) const
{
	if (!bHasStreamedChunkCoord)
	{
		return 0;
	}

	return FMath::Max(
		FMath::Abs(ChunkCoord.X - LastStreamedChunkCoord.X),
		FMath::Abs(ChunkCoord.Y - LastStreamedChunkCoord.Y));
}

void AVoxel::GetProxyGroupDistanceRange(const FIntPoint& GroupCoord, int32& OutMinDistance, int32& OutMaxDistance) const
{
	OutMinDistance = 0;
	OutMaxDistance = 0;

	if (!bHasStreamedChunkCoord)
	{
		return;
	}

	OutMinDistance = TNumericLimits<int32>::Max();
	const FIntPoint GroupOriginChunkCoord = GetProxyGroupOriginChunkCoord(GroupCoord);
	for (int32 LocalX = 0; LocalX < FMath::Max(1, ProxyGroupChunkSpan); ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < FMath::Max(1, ProxyGroupChunkSpan); ++LocalY)
		{
			const FIntPoint ChunkCoord(GroupOriginChunkCoord.X + LocalX, GroupOriginChunkCoord.Y + LocalY);
			const int32 Distance = GetChunkRingDistance(ChunkCoord);
			OutMinDistance = FMath::Min(OutMinDistance, Distance);
			OutMaxDistance = FMath::Max(OutMaxDistance, Distance);
		}
	}

	if (OutMinDistance == TNumericLimits<int32>::Max())
	{
		OutMinDistance = 0;
	}
}

int32 AVoxel::GetReadySourceChunkCountForProxyGroup(const FIntPoint& GroupCoord) const
{
	int32 ReadySourceChunkCount = 0;
	const FIntPoint GroupOriginChunkCoord = GetProxyGroupOriginChunkCoord(GroupCoord);
	for (int32 LocalX = 0; LocalX < FMath::Max(1, ProxyGroupChunkSpan); ++LocalX)
	{
		for (int32 LocalY = 0; LocalY < FMath::Max(1, ProxyGroupChunkSpan); ++LocalY)
		{
			const FIntPoint ChunkCoord(GroupOriginChunkCoord.X + LocalX, GroupOriginChunkCoord.Y + LocalY);
			const FVoxelChunkRuntimeEntry* ChunkEntry = ChunkRuntimeEntries.Find(ChunkCoord);
			if (ChunkEntry == nullptr
				|| !ChunkEntry->bIsDesired
				|| ChunkEntry->State != EVoxelChunkRuntimeState::Ready
				|| !ChunkEntry->CachedMeshData.IsValid())
			{
				continue;
			}

			++ReadySourceChunkCount;
		}
	}

	return ReadySourceChunkCount;
}
