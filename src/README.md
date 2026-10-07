# 2027-Dashboard
Dashboard of the Formula E SAE car.

## Dev notes
So far, the code has been copied over from previous development of the dashboard from 2024 and has not been updated to reflect the new car's parameters.

The active display is a single telemetry dashboard based on
`dash-template.drawio.html`. It uses the 800 x 480 EVE display area with a
status header, speed bar, speed readout, battery/current, brake and accelerator
cards, battery/high-voltage/low-voltage status cards, ready-to-drive status,
and warning banner. The legacy touch-screen demo states are no longer rendered.

## Serial debug commands

The dashboard listens on `Serial` at `115200` baud. Commands are newline
terminated:

```text
SET spd=100 brt=2
SET 100 2 80 0 0 12 1 400
RD spd brt hi_v
DMP
```

Named assignments can be supplied in any order. Positional `SET` values map
in this order:

`spd brt tmp brake accel low_v rtd hi_v`

`RD` prints the requested variables as `name=value`. `DMP` prints all
supported variables. Values outside their variable's storage range, unknown
variables, and malformed commands return an `ERR` response.