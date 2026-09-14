# Calibration Guide

A practical calibration model for a vision-guided pick-and-place platform (gantry with a
downward camera + a fixed upward camera, a nozzle, and stations). It maps what the vision
tool measures in **pixels** to **machine world coordinates** in millimetres.

> This is the customer-facing overview. Implementation-specific procedure details are
> provided with the runtime.

## Coordinate systems

| Frame | Origin | Axes | Unit |
|---|---|---|---|
| World (machine) | machine zero | +X right, +Y forward | mm |
| Image | top-left | +u right, +v **down** | px |
| Camera physical | image center | +X right, +Y up | mm |

`pixel_scale` (`s_u`, `s_v`, mm/px) and the **camera install angle** `θ_c` (angle between the
image u-axis and machine X) relate the camera frame to the world frame.

## What gets calibrated

| Item | Parameters | Refactor when |
|---|---|---|
| Downward-camera intrinsics | `s_u, s_v, θ_c, cx, cy, k1` | camera/lens changed |
| Upward-camera intrinsics | `s_u, s_v, θ_c, cx, cy, k1` | camera/lens changed |
| Upward-camera world position | `x_uc, y_uc, z_uc` | upward camera moved |
| Nozzle↔camera offset | `Δx_nc, Δy_nc, θ_n0` | nozzle/gantry adjusted |
| Station origin (per station) | `x, y, z` | fixture moved |

**Order:** intrinsics (down) → intrinsics (up) → upward world coords → nozzle offset → station
origins. Changing any item invalidates its downstream dependents.

## Runtime transform (pixel → pick pose)

```
1. du = x_p - cx ;  dv = y_p - cy                 # offset from image center (px)
2. dx_c =  du * s_u ;  dy_c = -dv * s_v           # camera-frame mm (sign of dv ↔ world +Y: verify)
3. dx_w = dx_c*cos(θ_c) - dy_c*sin(θ_c)           # rotate into world axes
   dy_w = dx_c*sin(θ_c) + dy_c*cos(θ_c)
4. x_target = x_w + dx_w ;  y_target = y_w + dy_w # x_w,y_w = gantry pose at capture
5. x_pick = x_target - Δx_nc ;  y_pick = y_target - Δy_nc   # nozzle lands over target
6. r_world = r_p + θ_c ;  r_cmd = r_world - θ_n0  # nozzle rotation command
```

A telecentric lens keeps `s_u/s_v` independent of Z within the depth of field, so no Z scale
compensation is needed.

## Calibration quality

| Metric | Acceptable | Excellent |
|---|---|---|
| Reprojection error | < 0.1 px | < 0.05 px |
| Nozzle-offset sampling std | < 0.005 mm | — |
| Station validation error | < 0.02 mm | — |

A quick pre-production check: re-image a known fiducial and compare the computed position to
the recorded value — warn at > 0.05 mm, stop at > 0.1 mm.

## `calibration.json`

```jsonc
{
  "version": "1.0",
  "dot_array": { "pitch_mm": 2.0, "dot_diameter_mm": 0.8, "cols": 11, "rows": 9 },
  "cameras": {
    "cam_down_1": {
      "image_width": 2592, "image_height": 1944,
      "pixel_scale_u": 0.002739, "pixel_scale_v": 0.002741,
      "install_angle_deg": 0.152,
      "principal_cx": 1296.0, "principal_cy": 972.0,
      "distortion_k1": 0.00012, "distortion_k2": 0.0,
      "reprojection_error_px": 0.048, "calibrated": true
    }
  },
  "upward_cameras": {
    "cam_up_1": { "pixel_scale_u": 0.003125, "pixel_scale_v": 0.003127,
                  "install_angle_deg": -0.088,
                  "world_x_mm": 152.340, "world_y_mm": 83.760, "world_z_mm": 0.0,
                  "reprojection_error_px": 0.061, "calibrated": true }
  },
  "nozzles": {
    "nozzle_1": { "associated_camera": "cam_down_1",
                  "offset_x_mm": 35.412, "offset_y_mm": 0.018,
                  "angle_zero_offset_deg": 0.28, "calibrated": true }
  },
  "stations": {
    "place_board_1": { "camera": "cam_down_1",
                       "origin_x_mm": 205.110, "origin_y_mm": 51.380, "origin_z_mm": -27.800,
                       "search_radius_mm": 3.0, "calibrated": true }
  }
}
```

- `origin_x/y/z` — gantry coordinate where the target should appear near the image center.
- `search_radius_mm` — max allowed target deviation from `origin` (beyond ⇒ NG).
- `offset_x_mm` (= Δx_nc) — from camera center to nozzle center in world coordinates.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Fewer dots detected than expected | over/under-exposed | adjust lighting/exposure |
| Many noise blobs | threshold/circularity filters | tighten them |
| High reprojection error | distortion unmodeled, or z inconsistent with production | model distortion; calibrate at the production Z |
| Fixed-direction pick offset | `Δx_nc/Δy_nc` error | re-calibrate the nozzle offset |
| Offset grows with position | `θ_c` error | re-calibrate camera intrinsics |
| Offset grows with angle | `θ_n0` error | re-calibrate the angle zero |
| Non-repeatable offset | mechanical looseness | tighten the nozzle/camera mounts |

## Sign convention check

If the world +Y direction is uncertain, move the gantry by a known +Y and observe whether a
fixed image point moves down (`dv > 0` ⇒ `dy_c = +dv·s_v`) or up (`dv < 0` ⇒ `dy_c = -dv·s_v`),
then fix the sign in step 2 above.
