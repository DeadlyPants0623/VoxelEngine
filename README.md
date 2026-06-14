# VoxelEngine

VoxelEngine is an Unreal Engine 5.7 sandbox project focused on procedural voxel terrain generation, chunk streaming, meshing, and runtime performance stats.

## Requirements

- Unreal Engine **5.7**
- A desktop GPU capable of running UE5 rendering features enabled by this project

## Getting Started

1. Open `VoxelEngine.uproject` in Unreal Engine 5.7.
2. Let Unreal generate project files if prompted.
3. Open the default map (`/Game/Map/NewTestMap`).
4. Click **Play**.

## Controls

- `W / A / S / D` — Move forward/left/back/right
- `Q / E` — Move down/up
- `Mouse` — Look around
- `Mouse Wheel` — Adjust fly speed
- `F10` — Toggle voxel stats detail

## Project Structure

- `Source/VoxelEngine/Public` — Public headers for voxel systems
- `Source/VoxelEngine/Private` — Runtime implementation (streaming, chunk generation, meshing, UI overlay)
- `Content` — Unreal assets and maps
- `Config` — Project and input settings

## Notes

- Default game mode: `SandboxGameMode`
- Default pawn: `FlyCameraPawn`
- Default startup map: `/Game/Map/NewTestMap`
