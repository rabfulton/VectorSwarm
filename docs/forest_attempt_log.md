# Forest Attempt Log

This file is a running log of forest-biome debugging attempts so we can distinguish:
- what was changed
- what the intended diagnosis was
- whether the result actually changed the output

It is intentionally separate from [`forest_lessons_learned.md`](/home/rab/code/v-type/docs/forest_lessons_learned.md).
This log is for chronology and avoiding repeated dead ends.

## Current Active Attempt

### 2026-03-06: Flora-pass optimization-bound fix

Goal:
- test the hypothesis that visible top cropping was caused by an optimization reject bound that was smaller than the real rendered silhouette

Change made:
- In [`forest_flora.frag`](/home/rab/code/v-type/src/shaders/forest_flora.frag), the early reject bounds in `trunk_layer(...)` were widened to use actual pod/umbrella radii instead of coarse fixed estimates.
- Added:
  - `pod_rx`
  - `umb_rx`
- Changed:
  - `shape_y_min`
  - `shape_y_max`
  - `shape_x_max`
- Reused the same radii in the later pod/umbrella shape math.

Reason:
- The previous bound optimization used fixed estimates like:
  - `shape_y_min = y_top - height * 0.12`
  - `shape_x_max = width * 5.0`
- Those were smaller than the real pod/umbrella silhouette in some cases.
- This caused the shader to `continue` before evaluating the true upper cap shape, producing apparent top cropping.

Observed result:
- succeeded
- user confirmed this fixed the clipping bug

Conclusion:
- the clipping bug was caused by an overly aggressive optimization envelope in the flora pass, not by top-margin placement alone
- future clipping work should inspect early reject bounds before adjusting placement logic

## Previous Attempts

### 2026-03-06: Flora-pass only top-bound fix

Goal:
- test whether the visible top cropping appears directly in `forest debug: flora`
- if so, fix only the flora pass first

Change made:
- In [`forest_flora.frag`](/home/rab/code/v-type/src/shaders/forest_flora.frag), the `top_extent` used for placement now also includes the actual rendered top bound:
  - added `top_extent = min(top_extent, y_top - height * 0.12);`

Reason:
- The shader was clamping with one top estimate, then rendering/culling against a higher top bound `shape_y_min = y_top - height * 0.12`.
- That mismatch can leave top cropping visible even when the plant was “pushed down”.

Expected verification:
- `forest debug: flora` should change if the flora pass is truly responsible for the visible top crop.

Observed result:
- failed
- `forest debug: flora` still shows top cropping after this change
- this means the simple `top_extent` vs `shape_y_min` mismatch was not the sole cause of the visible crop

Conclusion:
- do not repeat this same fix again as a standalone solution
- the remaining crop in `flora` must come from some other part of the plant shape math or from how the flora texture is sampled/displayed in the debug path

### Repeated top-margin-only fixes

What was tried:
- increase or adjust top margin
- push caps down based on rough `y_top` / `cap_y` estimates

Why it failed:
- these attempts did not necessarily use the final rendered top bound
- some of them also touched the wrong pass

Status:
- not reliable

### Main-pass hero overlay top fixes

What was tried:
- remove upper-band reject in `hero_fauna_overlay(...)`
- push down hero caps from their top extent

Why it did not settle the issue:
- the user later reverted these
- at the time, flora-pass clipping was still not fully ruled out

Status:
- unresolved as a separate step

### Neighbor-range expansion in flora pass

What was tried:
- change flora cell iteration from `-1..1` to `-2..2`

Reason at the time:
- hypothesis that wide caps were being clipped by not evaluating enough neighboring cells

Outcome:
- user reported no change
- reverted

Status:
- dead end for the current top-cropping bug

### Multiple assumptions about edge clipping

What was assumed:
- the crop might be horizontal edge clipping rather than top clipping

Outcome:
- user explicitly clarified the issue is only top cropping

Status:
- rejected diagnosis

## Notes

- `forest debug: flora` is the authoritative check for whether the flora prepass itself is still cropping.
- `forest debug: hero` should be used next only if `flora` becomes clean but full render still crops.
