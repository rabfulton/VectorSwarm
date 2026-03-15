# Frame Pacing Plan

This document captures the implementation plan for improving frame pacing and output smoothness in `v-type` without shifting focus to shader micro-optimization.

The priorities here are:

1. Remove structural CPU/GPU lockstep.
2. Remove blocking behavior from active frame paths.
3. Stabilize dynamic upload behavior.
4. Improve perceptual smoothness with render interpolation.
5. Keep resource creation/recreation out of steady-state gameplay.

## Current Structural Issues

### 1. One frame in flight

The renderer currently uses one global:

- `image_available` semaphore
- `render_finished` semaphore
- `in_flight` fence

and waits that fence every frame before acquiring the next image.

Effect:

- CPU and GPU are hard-serialized.
- Any long GPU frame immediately stalls the CPU.
- Frame pacing becomes more sensitive to transient GPU spikes.

Relevant code:

- `src/main.c:create_sync(...)`
- `src/main.c:record_submit_present(...)`
- `src/main.c:main(...)`
- `src/main.c:create_vg_context(...)`

### 2. Blocking upload behavior in DefconDraw Vulkan backend

The `vg` Vulkan backend currently:

- grows a single vertex buffer on demand
- calls `vkDeviceWaitIdle()` before reallocating it
- maps, copies, and unmaps vertex memory every frame

Effect:

- rare but severe hitches when the buffer grows
- avoidable per-frame CPU/GPU synchronization pressure
- no clean path to multi-frame upload ownership

Relevant code:

- `DefconDraw/src/backends/vulkan/vg_vk.c:vg_vk_ensure_vertex_buffer(...)`
- `DefconDraw/src/backends/vulkan/vg_vk.c:vg_vk_upload_vertices(...)`

### 3. No render interpolation

The game sim runs at a fixed step, but rendering uses the latest sim state directly.

Effect:

- motion can appear stepped even when frame time is stable
- high-refresh output is smoother than low-refresh output, but still not maximally smooth
- improvements to frame pacing will not fully translate into perceived smoothness unless interpolation is added

Relevant code:

- `src/main.c` fixed-step loop
- `src/render.h:render_metrics`
- `src/render.c`

### 4. Runtime resource work in the frame loop

Some resource validation/loading remains in the steady-state render loop.

Effect:

- normally cheap, but dangerous if a resource change occurs during active play
- can introduce visible stalls unrelated to actual frame rendering

Relevant code:

- `src/main.c:ensure_active_structure_tile_resources(...)`
- frame loop call site in `src/main.c`

## Implementation Plan

## Phase 1: Two Frames In Flight

Goal:

- decouple CPU submission from GPU completion enough to absorb transient long frames
- preserve existing behavior as much as possible while removing hard lockstep

Target model:

- `FRAME_OVERLAP = 2`
- per-frame sync objects
- per-frame command-buffer ownership
- per-frame upload ownership

### 1. Introduce frame-context structs

Add a small per-frame struct in `src/main.c`, for example:

```c
typedef struct frame_sync {
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
} frame_sync;
```

and a frame index:

```c
uint32_t current_frame;
frame_sync frames[2];
```

If later needed, extend this to include per-frame command buffer, per-frame query pool, and per-frame transient upload state.

### 2. Track swapchain-image fence ownership separately

Add per-image tracking:

```c
VkFence images_in_flight[APP_MAX_SWAPCHAIN_IMAGES];
```

Purpose:

- if an acquired swapchain image is still in flight from an older frame, wait only on that image's fence
- avoid assuming one global fence protects all swapchain images

### 3. Update sync creation/destruction

Change `create_sync(...)` to allocate two semaphore pairs and two fences instead of one.

Destroy all of them in cleanup paths.

### 4. Update frame loop ordering

Current shape:

- wait one fence
- acquire image
- record
- reset same fence
- submit
- present

Replace with standard two-frame pattern:

1. Select `frame = frames[current_frame]`
2. Wait `frame.in_flight`
3. Acquire swapchain image using `frame.image_available`
4. If `images_in_flight[image_index] != VK_NULL_HANDLE`, wait that fence
5. Set `images_in_flight[image_index] = frame.in_flight`
6. Reset `frame.in_flight`
7. Submit using `frame.image_available` and `frame.render_finished`
8. Present waiting on `frame.render_finished`
9. Advance `current_frame = (current_frame + 1) % FRAME_OVERLAP`

### 5. Update VG context configuration

Change:

- `desc.api.vulkan.max_frames_in_flight = 1`

to:

- `desc.api.vulkan.max_frames_in_flight = 2`

This should happen together with the backend upload rework below. If done alone, it improves the API contract but not the backend ownership model.

### 6. Validation target

After Phase 1:

- game still builds and runs
- no use-after-submit on command buffers or uploads
- no visual regressions in gameplay/menu/editor
- frame pacing should be less sensitive to occasional long GPU frames

## Phase 2: Remove Blocking VG Upload Behavior

Goal:

- no `vkDeviceWaitIdle()` in active rendering
- no shared mutable upload buffer with implicit single-frame ownership

This is the highest-risk structural issue after frames-in-flight.

### 1. Fix memory-type selection correctness

Before any structural rework, fix the backend bug where upload memory type is chosen against `0xffffffff` instead of actual `memoryTypeBits`.

Correct behavior:

- call `vkGetBufferMemoryRequirements(...)`
- choose a memory type from `req.memoryTypeBits`
- require host visible/coherent for upload memory

This should be done whether or not the rest of the upload redesign lands immediately.

### 2. Replace one global vertex buffer with per-frame upload buffers

Recommended direction:

- one upload buffer per frame in flight
- each persistently mapped at creation
- each sized for expected peak vector geometry

Suggested backend struct change in `DefconDraw/src/backends/vulkan/vg_vk.c`:

```c
typedef struct vg_vk_frame_upload {
    vg_vk_gpu_buffer vertex_buffer;
    void* mapped;
} vg_vk_frame_upload;
```

and:

```c
vg_vk_frame_upload frame_uploads[VG_MAX_FRAMES_IN_FLIGHT];
uint32_t frame_slot;
```

The exact array sizing should follow the backend's configured `max_frames_in_flight`.

### 3. Advance upload ownership each frame

At `vg_begin_frame(...)` or equivalent frame-start point:

- choose backend upload slot from frame index modulo frames-in-flight
- reset only CPU-side counters for that slot
- do not destroy/recreate buffers during active frames

### 4. Remove `vkMapMemory` / `vkUnmapMemory` from the steady-state path

Persistently map each host-visible upload buffer once at creation time.

Per frame:

- memcpy into the already-mapped region
- flush only if memory is not coherent

Current code assumes coherent memory, which is acceptable if preserved. If non-coherent support is added later, explicit flushes can be introduced.

### 5. Replace grow-on-demand stall with grow-once or deferred growth

Preferred behavior:

- choose a practical initial capacity based on current workload
- if exceeded, allocate a larger replacement buffer for future frames without `vkDeviceWaitIdle()`
- switch over only when safe by frame ownership

Pragmatic first step:

- significantly overprovision initial size
- only resize outside active gameplay or during explicit renderer recreation

That avoids stall spikes immediately, even before a fully elegant allocator lands.

### 6. Validation target

After Phase 2:

- no `vkDeviceWaitIdle()` in any steady-state frame submission path
- no per-frame map/unmap on the `vg` vector upload path
- vector rendering still matches current output

## Phase 3: Add Render Interpolation

Goal:

- improve perceived smoothness independently of GPU utilization
- make motion smoother at 120 Hz and above

### 1. Add interpolation alpha to render metrics

Extend `render_metrics` with:

```c
float sim_alpha;
```

where:

- `sim_alpha = sim_accum_s / sim_fixed_dt_s`
- clamped to `[0, 1]`

### 2. Introduce previous/current render state for interpolated objects

Do not try to interpolate the entire `game_state` struct blindly.

Instead, add explicit render snapshots for the objects that matter visually:

- player position/orientation
- enemies
- bullets
- enemy bullets
- camera
- particles if needed

Recommended approach:

- store previous transform state before each fixed sim update
- store current transform state after update
- render as `lerp(prev, curr, sim_alpha)`

### 3. Keep simulation authoritative

Interpolation should be render-only.

Do not:

- change gameplay collision timing
- change spawn timing
- modify fixed-step simulation behavior

### 4. Apply interpolation first where it matters most

Start with:

- camera
- player
- enemies
- projectiles

These deliver most of the visible benefit.

### 5. Validation target

After Phase 3:

- gameplay logic remains unchanged
- motion appears smoother at steady 120 Hz
- no visible rubber-banding or temporal lag

## Phase 4: Remove Runtime Resource Creation from Steady-State Loop

Goal:

- make the steady-state frame loop purely update/record/submit/present

### 1. Identify all frame-loop resource guards

Current known item:

- `ensure_active_structure_tile_resources(...)`

There may be others worth auditing after the primary work lands.

### 2. Push resource changes to explicit transition points

Examples:

- menu transitions
- level load
- atlas change in editor
- swapchain recreation

If a resource must change during play, do it through an explicit “renderer dirty” path, not opportunistically every frame.

### 3. Preserve fail-fast behavior

Do not add hidden fallback behavior.

If a required resource is missing:

- fail explicitly
- log clearly
- rebuild at a deliberate synchronization point

That matches project policy in `AGENTS.md`.

### 4. Validation target

After Phase 4:

- no heavyweight create/destroy path runs from the main steady-state render loop
- level/editor transitions remain correct

## Phase 5: Instrument Frame Pacing

Goal:

- verify pacing improvements with data
- avoid relying on average FPS alone

### 1. Add CPU frame timing metrics

Track and optionally display:

- frame time
- sim time
- record time
- submit/present wait time

### 2. Add GPU timing per major pass

Use Vulkan timestamp queries around:

- scene pass
- split background passes
- bloom pass
- composite pass

This is not for shader micro-optimization. It is to confirm whether pacing spikes come from:

- GPU work expansion
- queue backpressure
- synchronization stalls

### 3. Watch percentile frame time, not only FPS

Primary success metrics:

- lower 99th percentile frame time
- reduced max frame spikes
- less present jitter under steady play

## Suggested Execution Order

Implement in this order:

1. Fix VG memory-type selection correctness.
2. Introduce two frames in flight in app sync/submission path.
3. Rework VG dynamic uploads to per-frame, persistently mapped buffers.
4. Add render interpolation.
5. Remove frame-loop resource creation checks.
6. Add timing instrumentation for verification.

## Risk Notes

### Multi-frame risks

- offscreen resources are currently shared globally
- command buffer and upload ownership assumptions are currently one-frame oriented
- resource lifetime bugs may only appear under load

Mitigation:

- switch to two frames, not three, initially
- keep per-frame ownership explicit
- validate on menu, gameplay, level editor, and swapchain recreate

### Interpolation risks

- stale state if previous/current snapshots are not updated consistently
- camera and entity interpolation can diverge if not sourced from the same sim boundaries

Mitigation:

- centralize snapshot capture around fixed-step update boundaries
- start with a narrow interpolated set

### DefconDraw backend risks

- this is shared renderer infrastructure
- backend changes affect all vector drawing

Mitigation:

- keep initial redesign minimal
- avoid parallel upload systems if one clean per-frame system can serve all vector draws

## Definition Of Done

This work is complete when:

- steady-state gameplay has no active-frame `vkDeviceWaitIdle()`
- the renderer uses two frames in flight safely
- vector uploads do not map/unmap every frame
- render interpolation is active for key moving objects
- no heavyweight resource creation remains in the steady-state frame loop
- frame pacing is measurably more stable, especially in 99th percentile frame time
