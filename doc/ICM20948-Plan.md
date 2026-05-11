# ICM-20948 Integration Plan

## Goal
Add an ESP32-compatible IMU integration based on ICM-20948 to the gateway firmware with a safe rollout path:
- Phase 1: stable Attitude (PGN 127257) and optional Rate of Turn (PGN 127251).
- Phase 2: optional Heading (PGN 127250) only after calibration quality is validated.

## Why ICM-20948
- Widely available and actively used in ESP32 projects.
- Multiple Arduino-ready libraries exist.
- Provides accel, gyro, and magnetometer data for full 9-DoF processing.

## Scope
### In scope
- New user task for IMU processing and NMEA2000 output.
- Config items for enable, interval, mounting, calibration offsets, and output selection.
- Initial orientation/fusion path suitable for marine attitude reporting.
- Runtime counters and diagnostics.

### Out of scope (Phase 1)
- NMEA0183 heading sentence generation from this IMU path.
- Full automatic calibration wizard in web UI.
- Autopilot-grade heading guarantees without sea-trial validation.

## Architecture
### Placement
Implement as a user task under `lib/icm20948task` to avoid risky core refactoring.

### Data flow
1. I2C sensor read at configurable interval.
2. Apply axis mapping based on mounting configuration.
3. Apply fusion/filtering to produce orientation values.
4. Send NMEA2000:
   - PGN 127257 (Attitude): yaw, pitch, roll.
   - PGN 127251 (Rate of Turn): optional.
   - PGN 127250 (Heading): optional and disabled by default in phase 1.

### Existing APIs to use
- `SetN2kAttitude` for PGN 127257.
- `SetN2kRateOfTurn` for PGN 127251.
- `SetN2kMagneticHeading` for PGN 127250.
- `api->sendN2kMessage` for output dispatch.

## Configuration Plan
Add task config in `lib/icm20948task/config.json` with at least:
- enable switch.
- bus selection and I2C address.
- measurement interval (ms).
- output toggles: attitude, rate-of-turn, heading.
- mounting orientation fields (axis swap/invert or predefined orientations).
- basic calibration offsets:
  - gyro bias x/y/z.
  - accel bias x/y/z.
  - mag bias x/y/z.
- optional low-pass / smoothing parameters.

## Implementation Phases
## Phase 0: Library and build wiring
- Add `lib/icm20948task` with `GwIcm20948Task.h/.cpp`, `config.json`, optional local `platformio.ini`.
- Select one ICM-20948 Arduino library and pin a known working version.
- Ensure include/compile path is active through existing user-task discovery.

## Phase 1: Minimal functional output
- Initialize ICM-20948 over I2C.
- Read accel + gyro, optionally mag.
- Compute pitch/roll (and yaw placeholder if needed) with conservative filtering.
- Emit PGN 127257. Frequency? 5Hz? 
- Optionally emit PGN 127251 from gyro Z. Same Frequency?
- Add counters for init failures, read failures, sent PGNs.

Acceptance criteria:
- Task starts only when enabled.
- Continuous PGN 127257 visible on N2K bus. 
- Stable pitch/roll signs and scaling after static bench test.

## Phase 2: Heading and quality hardening
- Add magnetometer handling and tilt-compensated heading.
- Add calibration persistence and validity checks.
- Enable PGN 127250 only when calibration quality is above threshold.

Acceptance criteria:
- Heading drift reduced to acceptable level in static and slow-turn tests.
- No unstable heading spikes under normal vibration.

## Testing Plan
### Bench tests
- Power-on init and reconnect behavior.
- Static orientation checks at known angles.
- Axis orientation validation for all mounting presets.
- Fault tests: sensor unplugged, I2C error injection.

### Integration tests
- Verify N2K messages with a bus analyzer.
- Confirm no regression in existing NMEA0183/N2K conversion paths.
- Validate task CPU usage and timing impact.

### Sea-trial tests (required before enabling heading by default)
- Roll/pitch response under waves.
- Rate-of-turn plausibility versus known maneuvers.
- Heading stability under heel and acceleration.

## Risks and Mitigations
- Fusion instability under vibration:
  - Use conservative filtering defaults and runtime tunables.
- Magnetometer interference onboard:
  - Keep heading optional until calibration/placement is validated.
- Axis convention errors:
  - Provide mounting presets and explicit sign checks in test checklist.
- Resource impact on ESP32:
  - Keep loop lightweight; avoid blocking I2C patterns.

## Deliverables
- `lib/icm20948task/GwIcm20948Task.h`
- `lib/icm20948task/GwIcm20948Task.cpp`
- `lib/icm20948task/config.json`
- `lib/icm20948task/platformio.ini` (if additional deps are required)
- short operator notes in `doc/Sensors.md` (wiring, calibration, known limitations)

## Rollout Recommendation
- Merge with phase 1 defaults: heading off by default.
- Gather field logs from first installs.
- Enable phase 2 heading support after calibration and sea-trial validation.