# Weather operation hold

Automatic jobs use a separate weather hold, independent of manual pause and emergency stop.
A weather hold preserves the active jobs and freezes site equipment through the existing pause path.
Weather time, drainage, snowmelt and ice melt continue during this hold. Manual pause, emergency
stop and existing site faults retain their existing behavior; weather recovery never clears them.
Ship/cargo sea-motion poses wait with operations and refresh when work resumes.
Stage timeouts do not advance during the hold. Existing faults are not automatically reset.

The environment component exposes OperatingLimits in Environment / Operations. Defaults below are
simulation assumptions, not certified terminal or manufacturer operating limits.

| Quantity | Hold | Recovery requires |
| --- | --- | --- |
| Effective wind including gust | >= 10 m/s | < 8 m/s |
| Enabled fog visibility | <= 25 m | > 35 m or fog off |
| Enabled rainfall | >= 50 mm/h | < 40 mm/h or rain off |
| Enabled snowfall | >= 80 mm/h | < 60 mm/h or snow off |
| Accumulated ice fraction | >= 0.5 | < 0.35 |
| Effective tire static friction | <= 0.15 | > 0.2 |
| Enabled combined wave/swell Hs | >= 0.65 m | < 0.55 m or sea motion off |

All recovery conditions must remain satisfied for 10 simulation seconds. A new hazard resets
the recovery timer. Residual water, snow and ice remain relevant after precipitation switches off.
HUD shows the blocking reason and recovery timer. Logs emit WEATHER_HOLD and WEATHER_RESUME.

Wind changes over time. Rain, snow, fog and sea intensity currently remain at their selected
settings until changed; this feature does not generate a forecast or automatically end a storm.
Configure lower recovery thresholds than stop thresholds for upper-limit hazards, and a higher
recovery threshold for visibility and friction.

Automation: PortSim.Environment.OperatingHold covers rain, fog, snow, waves, wind, hysteresis,
interrupted recovery, residual ice, and continued environment evolution while waiting.

## Air temperature

Range: -25 to 40 degrees Celsius. Default: fixed 18 degrees Celsius.
Environment settings provide a reference-temperature slider and a daily-cycle toggle.
With the cycle enabled, the reference is the daily mean, with an assumed +/-4 degree
variation, maximum at 14:00 and minimum at 02:00; the initial model hour is 08:00.
Both the selected setting and the generated temperature are clamped to the range.
The cycle consumes simulation time, freezes on manual pause/emergency stop and continues
through weather holds. Fixed mode freezes the cycle clock and returns the selected value.

Saved settings: temperature_celsius, daily_temperature_cycle.
CLI overrides: -PortSimTemperature=-5 -PortSimDailyTemperature or -PortSimFixedTemperature.
Dashboard environment: temperature_celsius (current), temperature_setting_celsius (reference),
daily_temperature_cycle and temperature_hour. Missing legacy telemetry displays a dash.

Air temperature drives a simplified degree-day melt model; no pavement-temperature or
heat-balance calculation. At or below 0 C, snow and ice do not melt. Above 0 C the multiplier
is temperature / melt_reference_temperature_celsius (default 5 C). At 5 C, snow melts at
20 mm/h and full ice cover melts in 1800 seconds; at 10 C the rates double.
Melting continues during snowfall, with remaining snow bounded at zero. Meltwater enters
the existing water-film/drainage model. Warm air overrides the imposed icing target;
below freezing the icing switch can build ice, but switching it off preserves residual ice.
Daily-cycle thermal integration uses slices of at most one simulation second and midpoint
air temperature to resolve freezing-point crossings. Fixed-temperature reservoir integration
retains the exact event-based solution. Water/snow change telemetry covers the whole frame.
Automation: PortSim.Environment.TemperatureMelt.
