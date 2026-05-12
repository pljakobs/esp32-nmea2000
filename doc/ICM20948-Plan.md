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

### Output: NMEA2000 + UDP
NMEA2000 is for onboard NMEA network consumption. UDP is for logging, external display, and companion-system ingestion.

UDP output (format options):
- JSON (primary for InfluxDB ingestion and logging):
  - Send all raw and processed IMU data periodically to a configurable UDP endpoint (for example 192.168.1.20:5555).
  - Format: JSON with ISO 8601 timestamps (UTC).
  - Frequency: match sensor read interval or lower (configurable).

- Signal K (optional, for marine ecosystem compatibility):
  - If space permits, also support Signal K JSON format on same or separate UDP port.
  - Maps IMU data to standard Signal K paths: `navigation.attitude.*`, `navigation.rateOfTurn.*`, `navigation.magnetic*`, etc.
  - Enables integration with Signal K servers and other marine navigation software.
  - Frequency: configurable, typically lower than raw JSON to reduce bandwidth.

JSON payload includes:
- raw IMU: accel xyz, gyro xyz, mag xyz (if enabled), temperature.
- fused states: roll, pitch, yaw, rate-of-turn.
- quality metrics: accel validity, gyro validity, mag validity, heading confidence score.
- calibration metadata: active profile id, gyro/accel/mag bias offsets in use, soft-iron params.
- nav context aids: GNSS heading, onboard compass heading (if available on N2K), SOG/COG (if available).

Example UDP payload (JSON):
```json
{
  "timestamp": "2026-05-12T14:23:45.123456Z",
  "controller_id": "esp32-main",
  "sensor_id": "icm20948-0",
  "raw_imu": {
    "accel_mps2": [0.05, -0.12, 9.81],
    "gyro_rads": [0.001, -0.002, 0.003],
    "mag_ut": [25.3, -18.7, 42.1],
    "temp_c": 28.5
  },
  "fused_states": {
    "roll_rad": 0.1234,
    "pitch_rad": -0.0567,
    "yaw_rad": 2.8901,
    "rate_of_turn_rads": 0.0021
  },
  "quality": {
    "accel_valid": true,
    "gyro_valid": true,
    "mag_valid": true,
    "heading_confidence": 0.85,
    "quality_score": 82
  },
  "calibration": {
    "profile_id": "cruise_quiet",
    "gyro_bias_rads": [-0.0001, 0.0002, -0.0001],
    "accel_bias_mps2": [0.05, -0.03, 0.02],
    "mag_bias_ut": [2.1, -1.8, 0.9]
  },
  "nav_aids": {
    "gnss_heading_deg": 123.45,
    "gnss_heading_valid": true,
    "compass_heading_deg": 123.2,
    "compass_valid": true,
    "sog_kn": 5.2,
    "cog_deg": 123.1
  }
}
```

Example Signal K payload (alternative format, if enabled):
```json
{
  "context": "vessels.self",
  "updates": [
    {
      "timestamp": "2026-05-12T14:23:45.123456Z",
      "source": "imu.icm20948-0",
      "values": [
        {
          "path": "navigation.attitude.roll",
          "value": 0.1234
        },
        {
          "path": "navigation.attitude.pitch",
          "value": -0.0567
        },
        {
          "path": "navigation.attitude.yaw",
          "value": 2.8901
        },
        {
          "path": "navigation.rateOfTurn",
          "value": 0.0021
        },
        {
          "path": "navigation.headingMagnetic",
          "value": 2.1510
        },
        {
          "path": "navigation.magvar",
          "value": 0.0
        },
        {
          "path": "electrical.Raw.0.voltage",
          "value": 12.3
        }
      ]
    }
  ]
}
```


UDP configuration:
- Add config entries: `imu_udp_enable` (bool), `imu_udp_addr` (IP string), `imu_udp_port` (number).
- Optional: `imu_udp_interval_ms` (default = same as sensor interval).
- Optional: `imu_udp_format` (enum: "json" or "signalK", default = "json").
- If UDP send fails, log error counter but do not block sensor task.

Linux companion ingestion:
- Listen on configured UDP port.
- Parse JSON and time-synchronize with other sources.
- Insert into InfluxDB with tags/fields per Phase 4 schema.


## Configuration Plan
Add task config in `lib/icm20948task/config.json` with at least:
- enable switch.
- bus selection and I2C address.
- measurement interval (ms).
- output toggles: attitude, rate-of-turn, heading.
- UDP output: enable, target IP, port, interval (ms).
- mounting orientation fields (axis swap/invert or predefined orientations).
- basic calibration offsets:
  - gyro bias x/y/z.
  - accel bias x/y/z.
  - mag bias x/y/z.
- optional low-pass / smoothing parameters.

## Implementation Phases
### Implementation Strategy: Additive, Rebasing-Friendly

All changes are scoped to `lib/icm20948task/` as a standalone user task. No core firmware modifications.
- Use existing APIs from GwApi (N2K output, counters, config, logging).
- Leverage existing UDP channel infrastructure for output.
- Reuse existing config auto-discovery mechanism.
- Keep all IMU-specific code, config, and data in the task directory.

This allows clean rebasing against upstream without merge conflicts or risk of breaking existing features.

## Phase 0: Library and build wiring
- Add `lib/icm20948task` with `GwIcm20948Task.h/.cpp`, `config.json`, optional local `platformio.ini`.
- Select one ICM-20948 Arduino library and pin a known working version.
- Ensure include/compile path is active through existing user-task discovery.

## Phase 1: Minimal functional output
- Initialize ICM-20948 over I2C.
- Read accel + gyro, optionally mag.
- Compute pitch/roll (and yaw placeholder if needed) with conservative filtering.
- Emit PGN 127257 and optional PGN 127251 on NMEA2000 bus.
- Stream all raw and fused IMU data + quality metrics via UDP to Linux companion.
- Add counters for init failures, read failures, sent PGNs, UDP errors.

Acceptance criteria:
- Task starts only when enabled.
- Continuous PGN 127257 visible on N2K bus.
- UDP stream received on companion with correct payload format and timestamps.
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

## Phase 3: Calibration Wizard (Web UI + Task Support)

### Objectives
- Provide a guided calibration flow that works on real boats with vibration, wave motion, and magnetic disturbances.
- Separate one-time installation calibration from online quality monitoring.
- Output a quality score and only allow heading output when calibration quality is high enough.

### Wizard Overview
The wizard is split into 4 major stages:
1. Initial static calibration (mounting direction and angular offsets).
2. Initial dynamic calibration (gravity vector and gyro bias in low-motion windows).
3. Static compass correction (hard/soft iron + local magnetic model inputs).
4. In-route compass refinement (slow circles and optional figure-8 maneuvers).

Each stage stores:
- computed parameters,
- confidence score,
- timestamp,
- environment tags (engine state, charger/inverter state if available).

### Stage 1: Initial Static Calibration
Purpose:
- Determine sensor-to-boat alignment and fixed mount offsets.

User flow:
1. Select mounting preset (forward/up axis mapping) as a starting point.
2. Place boat as level as practical at dock/mooring.
3. Keep crew movement minimal for N seconds (e.g. 20-40s).
4. Collect samples and estimate mean roll/pitch offsets.
5. Ask user to confirm sign sanity (heel starboard = expected roll sign).

Algorithm notes:
- Use variance gating: reject windows with high gyro norm or accel variance.
- Estimate static offsets only if valid window coverage exceeds threshold.
- Save mount rotation matrix R_sb (sensor->boat) and zero offsets.

Pass criteria:
- sample count >= min count,
- accel norm close to 1g for accepted windows,
- residual tilt variance below threshold.

### Stage 2: Initial Dynamic Calibration (Low Motion)
Purpose:
- Improve gyro bias and gravity reference on real platform where perfectly static data is rare.

User flow:
1. Prompt user to run in calm water or sheltered marina at low speed.
2. Start 3-10 minute acquisition.
3. UI shows live motion quality meter (green/yellow/red).
4. Accept only green windows for bias estimation.

Algorithm notes:
- Gyro bias estimation from low-rate, low-variance segments.
- Gravity vector estimate from filtered accel only during low linear acceleration windows.
- Update accelerometer bias very slowly with robust estimator (Huber/median style weighting).

Quality gates per window:
- | |a| - g | < eps_a,
- |w| < eps_w,
- variance(a) < eps_var_a,
- variance(w) < eps_var_w.

Pass criteria:
- enough valid windows across time,
- bias estimate covariance below threshold,
- repeatability check passes on second short run.

### Stage 3: Static Compass Correction
Purpose:
- Correct magnetometer hard/soft iron effects for installed location.

User flow:
1. Auto-read latitude/longitude from NMEA2000 GNSS data (manual override available).
2. Collect magnetometer samples while slowly changing heading and heel (dock turns, gentle maneuvers).
3. Fit ellipsoid and compute correction parameters.
4. Display field-strength and fit residual diagnostics.

Inputs:
- lat/lon from GNSS on NMEA2000 bus (for expected Earth field model lookup),
- optional manual lat/lon override if GNSS is unavailable or suspected wrong,
- optional local magnetic variation value (if user has trusted source),
- optional manual heading offset.

GNSS source policy:
- Prefer valid GNSS position from N2K (e.g. PGN 129025/129029) with recent timestamp.
- Require quality gate before use (valid fix, age below timeout, non-NA coordinates).
- Freeze the position used for a calibration run once stage starts to keep model input stable.
- If GNSS quality drops during run, continue with frozen position and raise a warning.

Algorithm notes:
- Fit m_corr = A * (m_raw - b),
  - b: hard-iron vector,
  - A: soft-iron scale/skew correction matrix.
- Validate corrected norm against expected local field envelope.
- Reject outliers due to transient electrical loads.

Pass criteria:
- ellipsoid condition number within limits,
- residual error below threshold,
- corrected field norm stable and plausible.

### Stage 4: In-Route Compass Calibration (Slow Circles)
Question: will autopilot-style slow circles work?

Answer:
- Yes, they help and should be supported.
- They are effective for heading-dependent errors if done under controlled conditions.

Additional mode: commanded heading calibration runs
- Support a guided mode where helmsman/autopilot is asked to hold commanded headings (for example every 30 or 45 degrees).
- At each commanded heading, hold for a dwell time and collect stable samples.
- Use this as a complementary method to circles, especially when circles are impractical.

Reference sources for commanded heading mode:
- Primary: GNSS heading if available (dual-antenna GNSS or valid high-quality heading PGN).
- Fallback: GNSS COG only when speed is above minimum threshold and sideslip is expected low.
- Secondary cross-check: onboard magnetic compass heading on N2K, if known quality is good.

Reference quality gates:
- GNSS heading: valid status and update age below timeout.
- GNSS COG fallback: SOG above threshold (for example >2-3 kn), low turn acceleration, stable course window.
- Onboard compass: no active alarm/degraded state and innovation vs fused heading below threshold.

Reference selection policy:
- Use best available source by quality score (GNSS heading > onboard compass > GNSS COG fallback).
- If two sources are available, compute consistency residual and reject outlier source.
- If no source passes quality gates, pause calibration and instruct operator.

Required conditions:
1. Constant, low turn rate (no abrupt rudder changes).
2. Low pitch/roll sea state if possible.
3. Minimal nearby moving ferrous objects during run.
4. Stable electrical load profile during a calibration run.

Recommended maneuver protocol:
1. 2-4 full circles port and starboard, each 2-4 minutes.
2. Optionally add slow figure-8 for extra observability.
3. Repeat once with major electrical consumers on (radar/inverter/autopilot), if feasible.
4. Optionally run commanded headings sequence (e.g. 0/45/90/.../315 deg true or magnetic).
5. At each commanded heading, enforce dwell (e.g. 20-40s) and minimum sample quality before accepting point.

What this stage can improve:
- heading bias tables vs heading angle,
- residual hard/soft iron mismatch,
- load-state-specific heading offsets.
- alignment between IMU heading and vessel reference heading (GNSS/compass), including residual yaw offset.

What this stage cannot fully solve:
- rapidly changing magnetic fields from nearby switched high current paths,
- temporary ferrous items moved close to the sensor,
- severe mounting locations close to motors/transformers.

### Calibration Profiles (important on boats)
Store multiple compass profiles by electrical state:
- quiet profile (engine/charger/inverter mostly off),
- cruise profile (engine and normal loads on),
- heavy-load profile (high current consumers active).

Profile selection strategy:
- auto-select by sensed system state (preferred), or
- manual user select in UI.

### Runtime Quality Scoring and Heading Enable Policy
Compute continuous quality score Q in [0..100] from:
- accel/gyro consistency,
- magnetic norm plausibility,
- innovation residuals,
- recent disturbance events.

Policy:
- Q >= Q_on for T_on: heading output enabled.
- Q <= Q_off for T_off: heading output disabled/frozen.
- Keep hysteresis (Q_on > Q_off) to avoid chattering.

### Suggested Config Additions
Add config fields for:
- full gyro bias xyz,
- full accel bias xyz,
- full mag bias xyz,
- soft-iron matrix (or compact representation),
- mount rotation matrix or preset + yaw/pitch/roll offsets,
- heading quality thresholds (Q_on/Q_off),
- profile selection mode (auto/manual),
- calibration timestamps and validity flags.

### Wizard Deliverables
- Web UI pages/components for 4 stages, progress bars, and quality gauges.
- Backend API endpoints to start/stop stages and fetch live metrics.
- Persistent storage schema for calibration sets and profiles.
- Export/import calibration JSON for support and backup.

### After Those Stages: What Is Left?
After static, dynamic, and compass calibration, remaining work is mainly operational hardening:
1. Disturbance detection and graceful fallback logic (suppress bad heading, keep attitude/ROT).
2. Long-term drift monitoring and re-calibration reminders.
3. Sea-trial acceptance tests with logged quality metrics.
4. Installation guidance improvements (best sensor placement and cable routing).
5. Optional fusion aids (e.g. GNSS COG at speed) to stabilize yaw when magnetic quality is low.

In short: calibration gets you a good baseline, but robust heading on boats still depends on runtime quality gating, disturbance handling, and good sensor placement.

## Phase 4: Linux Companion Analytics (InfluxDB)

### Goal
- Keep ESP32 runtime deterministic and lightweight.
- Move heavy optimization and long-horizon model estimation to a Linux companion using archived data.

### System Split
Controller (ESP32):
- Real-time acquisition, fusion, and NMEA2000 output.
- Short-window quality gating and safe fallbacks.
- Execute latest accepted calibration/model parameters.

Companion (Linux + InfluxDB):
- Long-term data storage and query.
- Batch calibration refinement and vessel response model estimation.
- Parameter versioning, validation, and deployment back to controller.

### Data to Log (time-synchronized)
- Raw IMU: accel xyz, gyro xyz, mag xyz, temperature, sample timestamps.
- Fused states: roll/pitch/yaw, rate-of-turn, heading quality score, validity flags.
- N2K nav context: GNSS position, SOG/COG, GNSS heading if present, onboard compass heading.
- Vessel/system context: engine/load states if available, autopilot mode, calibration stage markers.

### Influx Schema Guidance
- Measurement groups:
  - imu_raw
  - imu_fused
  - nav_context
  - calibration_events
  - model_outputs
- Tags:
  - vessel_id, controller_id, sensor_id, profile_id, firmware_version.
- Fields:
  - numeric sensor/state values plus boolean quality flags.
- Keep all timestamps in UTC and store source latency if known.

### Batch Jobs on Linux
1. Nightly calibration refinement:
- Re-estimate gyro/accel/mag biases from accepted windows.
- Re-fit hard/soft iron model using disturbance-filtered samples.
- Recompute heading residual maps by heading sector and load profile.

2. Weekly vessel response update:
- Estimate empirical response model for heave/roll/pitch by speed and heading bins.
- Compute wave-related metrics from long windows and confidence intervals.

3. Health monitoring:
- Detect drift, sensor degradation, or persistent magnetic anomalies.
- Emit recalibration recommendations.

### Model/Parameter Lifecycle
1. Train candidate on historical data.
2. Validate on hold-out periods.
3. Compare against currently deployed version.
4. Promote only if objective metrics improve and safety checks pass.
5. Push to ESP32 as versioned parameter set.
6. Activate with rollback option.

### Deployment Interface to ESP32
- Define a compact calibration payload:
  - biases, mount matrix, soft-iron parameters, heading correction tables, thresholds.
- Include metadata:
  - model_version, created_at, valid_from, confidence score, source data range.
- Controller must verify checksum/version and persist atomically.

### Safety Policy
- Never apply new parameters immediately during unstable navigation.
- Stage new parameters as pending, activate only in safe condition or on reboot.
- If post-activation quality worsens beyond threshold, auto-rollback.

### What This Enables
- Better heading stability over months of operation.
- Data-driven correction for vessel-specific behavior and electrical load effects.
- Wave period/height estimates with confidence bounds from long-term statistics.

### Remaining Constraints
- Dynamic local magnetic disturbances near sensor cannot be fully eliminated by offline calibration.
- Absolute wave height offshore still depends on model quality and vessel response assumptions.
- Good sensor placement remains critical even with strong analytics.