#pragma once

#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelTerrainSettings.h"
#include "VoxelChunk.h"
#include "Voxel.generated.h"

class USceneComponent;
class UVoxelStatsOverlayWidget;
class AVoxelProxyChunk;
struct FConvexVolume;

enum class EVoxelChunkRuntimeState : uint8
{
	Queued,
	GeneratingVoxels,
	PendingMeshing,
	Meshing,
	Ready
};

enum class EVoxelProxyGroupRuntimeState : uint8
{
	Queued,
	Meshing,
	Ready
};

struct FVoxelChunkRuntimeEntry
{
	EVoxelChunkRuntimeState State = EVoxelChunkRuntimeState::Queued;
	TWeakObjectPtr<AVoxelChunk> ChunkActor;
	uint64 ActiveRequestId = 0;
	double LastRequestedTimeSeconds = 0.0;
	uint32 LastRequestedStreamingEpoch = 0;
	uint32 ActiveRequestStreamingEpoch = 0;
	bool bIsDesired = false;
	bool bIsInView = true;
	bool bIsInRenderFrustum = true;
	bool bIsRendered = true;
	bool bNeedsRemesh = false;
	bool bVoxelQueued = false;
	bool bMeshQueued = false;
	double LastVisibleTimeSeconds = 0.0;
	TSharedPtr<TArray<AVoxelChunk::FVoxel>, ESPMode::ThreadSafe> CachedVoxels;
	TSharedPtr<FVoxelChunkMeshData, ESPMode::ThreadSafe> CachedMeshData;
	FVoxelChunkBorderData BorderData;
	FVoxelTerrainSampleStats TerrainStats;
};

struct FVoxelChunkVoxelBuildResult
{
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;
	uint64 RequestId = 0;
	uint32 RequestStreamingEpoch = 0;
	TArray<AVoxelChunk::FVoxel> Voxels;
	FVoxelChunkBorderData BorderData;
	FTimespan BuildDuration = FTimespan::Zero();
	FVoxelTerrainSampleStats TerrainStats;
};

struct FVoxelChunkMeshBuildResult
{
	FIntPoint ChunkCoord = FIntPoint::ZeroValue;
	uint64 RequestId = 0;
	uint32 RequestStreamingEpoch = 0;
	FVoxelChunkMeshData MeshData;
	FTimespan BuildDuration = FTimespan::Zero();
};

struct FVoxelProxyGroupRuntimeEntry
{
	EVoxelProxyGroupRuntimeState State = EVoxelProxyGroupRuntimeState::Queued;
	TWeakObjectPtr<AVoxelProxyChunk> ProxyActor;
	uint64 ActiveRequestId = 0;
	double LastRequestedTimeSeconds = 0.0;
	uint32 LastRequestedStreamingEpoch = 0;
	uint32 ActiveRequestStreamingEpoch = 0;
	bool bIsDesired = false;
	bool bShouldRenderProxy = false;
	bool bIsRendered = false;
	bool bNeedsRebuild = true;
	bool bProxyQueued = false;
	uint32 Revision = 1;
	uint32 ActiveBuildRevision = 0;
	int32 ReadySourceChunkCount = 0;
	int32 SourceChunkCount = 0;
	int64 ApproximateMeshBytes = 0;
};

struct FVoxelProxyMeshBuildResult
{
	FIntPoint GroupCoord = FIntPoint::ZeroValue;
	uint64 RequestId = 0;
	uint32 RequestStreamingEpoch = 0;
	uint32 Revision = 0;
	FVoxelChunkMeshData MeshData;
	FTimespan BuildDuration = FTimespan::Zero();
	int32 SourceChunkCount = 0;
	int64 ApproximateMeshBytes = 0;
};

UCLASS()
class VOXELENGINE_API AVoxel : public AActor
{
	GENERATED_BODY()

public:
	AVoxel();

	// Legacy finite-grid setting retained for compatibility with older placed actors.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 NumChunks;

	// Chunk size (number of voxels per chunk)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 ChunkGridSize;

	// Voxel size
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	float VoxelSize;

	// Chunk height
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 ChunkHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 WorldSeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Terrain")
	FVoxelTerrainSettings TerrainSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	FTimespan GenerationTime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	FTimespan LastVoxelBuildTime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	FTimespan LastMeshBuildTime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	FTimespan LastChunkApplyTime;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 GeneratedChunkCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 TotalVoxelSlots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 TotalSolidVoxels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 TotalSurfaceVoxels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 TotalCulledInteriorVoxels;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 TotalVisibleFaces;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 TotalTriangles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 TotalVertices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	FString GenerationStatsSummary;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 QueuedChunkCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 GeneratingChunkCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 FinalizedChunksThisTick;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 DroppedStaleChunkResultCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 DroppedStaleVoxelResultCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 DroppedStaleMeshResultCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 RemeshedChunkCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 VisibleLoadedChunkCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 HiddenLoadedChunkCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 VisibleQueuedChunkCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 ActiveProxyGroupCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 QueuedProxyGroupCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 BuildingProxyGroupCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 VisibleProxyGroupCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 ProxyTransitionsThisTick;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 ChunkRenderVisibilityTransitionsThisTick;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int64 ProxyMeshBytes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 ActiveRealChunkJobCountStat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 ActiveProxyJobCountStat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 FirstChunkBuildCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 DroppedOutdatedResultCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 DeferredProxyJobStartCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 DeferredVoxelJobStartDueToMeshQueueCount;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 MinSampledTerrainHeight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	int32 MaxSampledTerrainHeight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Stats")
	float AverageSampledTerrainHeight;

	/** Refreshes chunk stats and the stats overlay immediately (e.g. after toggling detail mode). */
	void ForceRefreshGenerationStatsDisplay();

protected:
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0"))
	int32 LoadRadius;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 UnloadRadius;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0.01"))
	float StreamingUpdateInterval;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming")
	bool bAutoScaleStreamingBudgets;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 MaxConcurrentGenerationJobs;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 MaxQueuedJobStartsPerTick;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 MaxChunkFinalizationsPerTick;

	// Limits how many completed voxel builds are merged per tick (each can enqueue mesh + adjacent remesh fan-out).
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 MaxCompletedVoxelBuildsPerTick;

	// How many deferred streaming remesh requests to apply per tick (unload neighbors, re-requested ready chunks).
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 MaxDeferredChunkRemeshFlushesPerTick;

	// How many mesh completions may SpawnActor<AVoxelChunk> per tick; extras stay on the completion queue for later ticks.
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 MaxMeshSpawnFinalizationsPerTick;

	// When the mesh work queue reaches this size, pause starting new voxel generation jobs until it drains (0 disables).
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0"))
	int32 MeshQueueVoxelBackpressureThreshold;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0.0"))
	float ViewCullingGracePeriod;

	// Extra half-extent (cm) added to chunk bounds for render frustum tests only (reduces edge popping). Low sun / long shadows: combine with horizontal multiplier below.
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0.0", ToolTip = "Adds half-extent (cm) to chunk AABB for render-only frustum tests. Long shadows: raise slightly or use horizontal multiplier."))
	float ChunkViewRenderExtentPaddingCm;

	// Scales chunk X/Y half-extents before padding for render tests only. Try 1.05–1.25 if shadow casters behind you pop in/out.
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1.0", ToolTip = "Multiplies X/Y half-extents before padding for render tests only. Low sun: try 1.05–1.25 to keep shadow casters alive behind camera."))
	float ChunkViewRenderHorizontalExtentMultiplier;

	// Converts camera turn rate (deg/s) into extra padding (cm/s of padding). Set 0 to disable.
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0.0"))
	float ChunkViewRenderAngularSpeedPaddingScale;

	// Converts pawn speed (cm/s) into extra render padding (unitless scale * cm/s = cm of padding).
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0.0"))
	float ChunkViewRenderLinearSpeedPaddingScale;

	// 0 = unlimited. Shows are applied before hides when both are pending.
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0"))
	int32 MaxChunkRenderVisibilityTogglesPerTick;

	// Added to distance score when bIsInView is false (job queue). Lower = more background work; keep well above max squared ring distance.
	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "0"))
	int32 OutOfViewChunkJobPriorityPenalty;

	UPROPERTY(EditAnywhere, Category = "Voxel Streaming", meta = (ClampMin = "1"))
	int32 MaxConcurrentProxyJobs;

	UPROPERTY(EditAnywhere, Category = "Voxel Stats", meta = (ClampMin = "0.05"))
	float StatsRefreshInterval;

	UPROPERTY(EditAnywhere, Category = "Voxel HLOD", meta = (ClampMin = "1"))
	int32 ProxyGroupChunkSpan;

	UPROPERTY(EditAnywhere, Category = "Voxel HLOD", meta = (ClampMin = "0"))
	int32 ProxyRealChunkRadius;

	UPROPERTY(EditAnywhere, Category = "Voxel HLOD", meta = (ClampMin = "0"))
	int32 ProxyOnlyStartRadius;

	UPROPERTY(EditAnywhere, Category = "Voxel Collision", meta = (ClampMin = "0"))
	int32 CollisionChunkRadius;

	UPROPERTY(VisibleAnywhere, Category = "Voxel Components")
	USceneComponent* SceneRoot;

	UPROPERTY(Transient)
	UVoxelStatsOverlayWidget* StatsOverlayWidget;

	TMap<FIntPoint, FVoxelChunkRuntimeEntry> ChunkRuntimeEntries;
	TMap<FIntPoint, FVoxelProxyGroupRuntimeEntry> ProxyGroupRuntimeEntries;
	TArray<FIntPoint> QueuedVoxelChunkCoords;
	TArray<FIntPoint> QueuedMeshChunkCoords;
	TArray<FIntPoint> QueuedProxyGroupCoords;
	TSet<uint64> InFlightRequestIds;
	TSet<uint64> ActiveVoxelRequestIds;
	TSet<uint64> ActiveMeshRequestIds;
	TSet<uint64> ActiveProxyRequestIds;
	TQueue<FVoxelChunkVoxelBuildResult, EQueueMode::Mpsc> CompletedVoxelResults;
	TQueue<FVoxelChunkMeshBuildResult, EQueueMode::Mpsc> CompletedMeshResults;
	TQueue<FVoxelProxyMeshBuildResult, EQueueMode::Mpsc> CompletedProxyMeshResults;
	TSet<FIntPoint> DeferredChunkRemeshCoords;
	FIntPoint LastStreamedChunkCoord;
	bool bHasStreamedChunkCoord;
	uint32 CurrentStreamingEpoch;
	uint64 NextChunkRequestId;
	double LastStreamingRefreshTimeSeconds;
	double LastStatsRefreshTimeSeconds;
	int32 MeshSpawnFinalizationsThisTick;
	FVector LastViewBiasCameraForward;
	bool bHasLastViewBiasSample;

	float StatsOverlaySmoothedFrameMs = 0.0f;
	bool bStatsOverlaySmoothedFrameMsInit = false;
	int32 LastStatsOverlayDisplayMode = -1;

	// Create chunks
	void CreateChunks();
	void ClearChunks();
	void ResetGenerationStats();
	void EnsureStatsOverlay();
	void UpdateGenerationStatsDisplay();
	void UpdateStreamingChunks(bool bForceRefresh = false);
	void EnqueueVoxelChunk(const FIntPoint& ChunkCoord);
	void EnqueueMeshChunk(const FIntPoint& ChunkCoord);
	void EnqueueProxyGroup(const FIntPoint& GroupCoord);
	void QueueChunkRequest(const FIntPoint& ChunkCoord, double RequestTimeSeconds);
	void QueueProxyGroupRequest(const FIntPoint& GroupCoord, double RequestTimeSeconds);
	void StartQueuedVoxelJobs();
	void StartQueuedMeshJobs();
	void StartQueuedProxyMeshJobs();
	void ProcessCompletedVoxelBuilds();
	void ProcessCompletedMeshBuilds();
	void ProcessCompletedProxyMeshBuilds();
	void RequestDeferredChunkRemesh(const FIntPoint& ChunkCoord);
	void ProcessDeferredChunkRemeshes();
	void FinalizeChunkBuild(FVoxelChunkMeshBuildResult&& BuildResult);
	void FinalizeProxyGroupBuild(FVoxelProxyMeshBuildResult&& BuildResult);
	bool TryPopNextQueuedVoxelChunk(FIntPoint& OutChunkCoord);
	bool TryPopNextQueuedMeshChunk(FIntPoint& OutChunkCoord);
	bool TryPopNextQueuedProxyGroup(FIntPoint& OutGroupCoord);
	void UnloadChunk(const FIntPoint& ChunkCoord);
	void UnloadProxyGroup(const FIntPoint& GroupCoord);
	void RefreshChunkStats();
	void MarkChunkForRemesh(const FIntPoint& ChunkCoord);
	void MarkAdjacentChunksForRemesh(const FIntPoint& ChunkCoord);
	void MarkProxyGroupForRebuild(const FIntPoint& GroupCoord);
	void MarkProxyGroupsForChunkChange(const FIntPoint& ChunkCoord);
	void UpdateProxyGroupDesiredStates(double RequestTimeSeconds);
	FVoxelChunkNeighborBorderData GetNeighborBorderDataForChunk(const FIntPoint& ChunkCoord) const;
	int32 GetActiveAsyncJobCount() const;
	int32 GetActiveRealChunkJobCount() const;
	int32 GetActiveProxyJobCount() const;
	bool HasPendingRealChunkWork() const;
	bool ShouldRefreshStreamingChunks() const;
	bool ShouldRefreshStats() const;
	int32 GetGenerationJobLimit() const;
	int32 GetQueuedJobStartBudget() const;
	int32 GetChunkFinalizationBudget() const;
	int32 GetProxyJobLimit() const;
	void UpdateChunkVisibilityStates();
	void UpdateRenderArbitration();
	FVector ComputeChunkViewExtentInflation(float DeltaTime);
	bool IsChunkAabbIntersectingFrustum(const FIntPoint& ChunkCoord, const FConvexVolume& ViewFrustum, float HorizontalExtentMultiplier, const FVector& ExtentInflation) const;
	bool TryGetStreamingFocusLocation(FVector& OutLocation) const;
	bool TryBuildStreamingViewFrustum(FConvexVolume& OutViewFrustum) const;
	bool IsChunkInView(const FIntPoint& ChunkCoord, const FConvexVolume& ViewFrustum) const;
	float GetChunkWorldSize() const;
	int32 GetEffectiveUnloadRadius() const;
	int32 GetChunkPriorityScore(const FIntPoint& ChunkCoord) const;
	int32 GetProxyGroupPriorityScore(const FIntPoint& GroupCoord) const;
	int32 GetDesiredSourceChunkCountForProxyGroup(const FIntPoint& GroupCoord) const;
	uint64 ReserveRequestId();
	FIntPoint GetChunkCoordFromWorldLocation(const FVector& WorldLocation) const;
	FIntPoint GetProxyGroupCoordFromChunkCoord(const FIntPoint& ChunkCoord) const;
	FIntPoint GetProxyGroupOriginChunkCoord(const FIntPoint& GroupCoord) const;
	FVector GetChunkWorldLocation(const FIntPoint& ChunkCoord) const;
	FVector GetProxyGroupWorldLocation(const FIntPoint& GroupCoord) const;
	FVector2D GetChunkOffset(const FIntPoint& ChunkCoord) const;
	int32 GetChunkRingDistance(const FIntPoint& ChunkCoord) const;
	void GetProxyGroupDistanceRange(const FIntPoint& GroupCoord, int32& OutMinDistance, int32& OutMaxDistance) const;
	int32 GetReadySourceChunkCountForProxyGroup(const FIntPoint& GroupCoord) const;

	FString GenerationStatusText;
};
