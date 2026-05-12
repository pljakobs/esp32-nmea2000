# Daly BLE Battery Monitor Plan

## Goal
Add a minimal battery monitor to the firmware so a Daly BMS battery becomes visible on the NMEA2000 bus.

The minimum useful result is:
- live battery voltage on NMEA2000,
- live battery current on NMEA2000,
- battery temperature on NMEA2000 when available,
- a stable task that reconnects automatically after BLE errors.

State of charge is desirable, but it should be treated as phase 2 unless the required NMEA2000 helper is already available in the selected library version or is easy to add safely.

## Existing Reference
The external script at `/home/pjakobs/devel/daly_mon/dalymon.py` already shows the important functional shape:
- connect to the battery over BLE,
- use Daly-specific protocol support,
- poll a compact telemetry set,
- flatten the returned payload into scalar values.

That script uses Python-only components like `bleak` and `aiobmsble`, so it is not directly portable into this firmware. The firmware implementation should reuse the protocol knowledge and field selection, not the Python code.

## Architecture Decision
Implement this as a dedicated user task under a new directory like `lib/dalytask`.

This is the best fit for the current firmware because:
- user tasks are the intended extension point for new hardware integrations,
- the task can own its own polling loop and reconnect logic,
- the task can send NMEA2000 messages directly through `api->sendN2kMessage`,
- the integration stays isolated from the core converter paths.

## Minimum Design
### Task responsibilities
The task should do exactly these things in phase 1:
1. initialize BLE client support,
2. find and connect to the configured Daly BMS,
3. implement the minimum Daly request/response exchange needed to read pack data,
4. decode at least voltage, current, state of charge, and temperature from the BMS payload,
5. publish the decoded values as NMEA2000 battery data,
6. retry cleanly on disconnects, timeouts, or invalid frames.

### Recommended output path
Prefer direct NMEA2000 transmission from the task instead of routing through XDR.

Reason:
- the firmware already supports direct battery NMEA2000 sends from user tasks,
- this keeps the minimal implementation smaller,
- the battery becomes visible on the NMEA2000 bus even without adding UI work,
- XDR mapping can be added later if dashboard display on the web data page is needed.

### Minimum NMEA2000 PGNs
Phase 1 should send:
- `PGN 127508` Battery Status with voltage, current, and temperature.

Optional phase 1 shortcut:
- if the chosen NMEA2000 library version exposes a clean helper for battery SoC or battery configuration/status, add it.

If not, keep phase 1 limited to `127508`.

This is enough to satisfy the stated goal that the battery is visible on the NMEA2000 bus.

## BLE And Protocol Plan
### BLE layer
The firmware task needs to act as a BLE client.

There are two realistic implementation paths:
- use the BLE support already present in the Arduino ESP32 framework,
- or add `NimBLE-Arduino` if the default BLE stack proves too heavy or awkward.

Recommendation:
- start with a short spike to determine which client API gives the smallest and most stable implementation on the target board,
- prefer `NimBLE-Arduino` if RAM pressure or reconnect behavior becomes a problem.

### Protocol layer
Do not attempt a generic BMS abstraction first.

Instead, implement only the Daly protocol pieces required for the minimum telemetry set:
- connect,
- discover service/characteristic pair used by the Daly BMS,
- send request frame,
- collect reply frame,
- validate frame length and checksum,
- decode the needed fields.

The correct split is:
- BLE transport class handles connect, read, write, notify, and reconnect,
- Daly protocol class handles request framing and response decoding,
- task loop coordinates polling and NMEA2000 transmission.

## Proposed File Layout
- `lib/dalytask/GwDalyTask.h`
- `lib/dalytask/GwDalyTask.cpp`
- `lib/dalytask/GwDalyBleClient.h`
- `lib/dalytask/GwDalyBleClient.cpp`
- `lib/dalytask/GwDalyProtocol.h`
- `lib/dalytask/GwDalyProtocol.cpp`
- `lib/dalytask/config.json`
- `lib/dalytask/platformio.ini` if an extra BLE dependency is required
- optional later: `lib/dalytask/daly.js` for custom UI display

## Configuration Plan
Add a separate config category for the Daly task with at least:
- enable switch,
- battery name or label,
- NMEA2000 battery instance,
- poll interval in ms,
- BLE MAC address,
- optional service UUID override,
- optional RX/TX characteristic UUID overrides,
- reconnect base delay,
- debug logging switch.

For a true minimum implementation, hardcoded UUIDs are acceptable if the target BMS model is fixed and already known from the Python monitor.

## Implementation Phases
## Phase 0: Feasibility spike
Goal: prove that the board can connect to the Daly BMS over BLE and read one stable telemetry frame.

Tasks:
- add a temporary user task scaffold,
- bring up BLE client support,
- connect by configured MAC address,
- send one Daly query,
- log the raw response,
- verify reconnect after power cycling the BMS.

Acceptance criteria:
- repeated successful connect and poll on the target hardware,
- no watchdog resets,
- acceptable free heap after BLE startup.

## Phase 1: Minimum usable battery output
Goal: put the battery on the NMEA2000 bus with stable basic metrics.

Tasks:
- implement periodic polling,
- decode pack voltage,
- decode pack current,
- decode battery temperature,
- decode state of charge for internal logging even if not yet published as a PGN,
- send `PGN 127508`,
- add task counters for connect success, connect failure, poll success, poll timeout, decode failure, sent PGNs.

Acceptance criteria:
- battery is visible on the NMEA2000 bus,
- voltage and current track the Python monitor plausibly,
- disconnect and reconnect recover automatically,
- task does not block other firmware functions.

## Phase 2: Better battery semantics
Goal: add more battery information once phase 1 is stable.

Possible additions:
- publish state of charge on NMEA2000 if a suitable PGN/helper is available,
- expose pack alarms or problem bits as status data,
- publish remaining capacity or time remaining if the data is trustworthy,
- add XDR mapping or web UI hooks so the values show in the gateway data page.

## Reuse From Existing Firmware
The firmware already provides the pieces needed for a low-risk implementation:
- user tasks can be registered dynamically,
- user tasks can send NMEA2000 messages directly,
- user tasks can add counters and capabilities,
- the example task already demonstrates direct battery message sending,
- the converter layer already recognizes battery-related XDR categories if UI work is needed later.

This means the new work is mainly BLE transport plus Daly decoding, not new core architecture.

## Risks
### BLE memory footprint
BLE client support may be heavy on ESP32 RAM.

Mitigation:
- validate on the real target board first,
- prefer a lighter BLE stack if necessary,
- keep the polling payload small.

### WiFi and BLE coexistence
Running BLE polling together with both WiFi station mode and the local access point may increase radio contention and memory pressure.

Mitigation:
- if the WiFi client is connected successfully, shut down the local AP after a short grace period,
- target roughly 1 minute for this BLE use case,
- reuse the existing AP auto-shutdown path in `GwWifi` instead of inventing a Daly-specific workaround.

Note:
- the firmware already has a `stopApTime` mechanism for unused AP shutdown,
- current behavior is generic and not specifically tied to WiFi client connection,
- if BLE stability needs it, a small follow-up change can tighten this so the AP drops sooner once STA connectivity is established.

### Protocol uncertainty
The Python stack hides some Daly protocol details.

Mitigation:
- capture one known-good request/response pair from the Python monitor,
- implement only the commands needed for phase 1,
- keep protocol parsing strict and small.

### Reconnect behavior
Battery power saving or RF interruptions may drop the BLE link.

Mitigation:
- make every poll cycle tolerant of reconnect,
- use backoff on repeated failures,
- never block forever waiting for notifications.

### Publishing incomplete battery semantics
`127508` alone may not give every chartplotter a full battery page.

Mitigation:
- accept that for phase 1,
- extend later once the exact display requirements are known.

## Testing Plan
### Bench tests
- connect to the real Daly BMS by MAC address,
- compare voltage/current/temperature against the Python monitor,
- verify current sign convention while charging and discharging,
- verify recovery after BMS reboot,
- verify recovery after ESP32 reboot,
- run for several hours and inspect task counters.

### NMEA2000 integration tests
- inspect outgoing bus traffic with an analyzer,
- confirm `PGN 127508` appears at the configured battery instance,
- confirm no message storm is created,
- confirm the task does not break existing WiFi, CAN, or sensor behavior.

## Recommended First Coding Step
Start with a phase-0 spike, not the whole feature.

Specifically:
1. create `lib/dalytask` as a user task,
2. add only BLE connect plus one Daly poll,
3. log raw decoded voltage/current values,
4. only after that add `127508` transmission.

That is the cheapest way to disconfirm the main technical risk, which is not NMEA2000 but BLE plus Daly protocol viability on the ESP32 target.

## Definition Of Done For Minimum Version
The minimum feature is done when:
- the firmware can poll one configured Daly BMS over BLE without external help,
- the task reconnects automatically after failures,
- the task sends stable battery status onto the NMEA2000 bus,
- the values are close to the readings from the existing Python monitor.