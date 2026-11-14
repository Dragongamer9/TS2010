# Train physics: sources for the game's numbers

Research by Perplexity (Command Center teammate), 2026-11-12; saved, converted and checked by
Claude. Used by `src/sim/TrainPhysics` (the presets name their source in `source`).

## Resistance: R = A + B v + C v² (N, v in m/s, total per vehicle or train)

- **Freight wagon**: AAR RP-548 in metric form, F. Szanto, "Rolling Resistance Revisited",
  Conference on Railway Excellence 2016, eq. 4: "R = 6.5m + 80n + 0.046vm + 0.096 v² (N)"
  (m tonnes, n axles, v km/h) ([PDF](https://rosap.ntl.bts.gov/view/dot/79083/dot_79083_DS1.pdf)).
  In m/s: A = 6.5 m + 80 n, B = 0.1656 m, C = 1.24416 (the squared term converts as
  0.096 × 3.6²). Checked: equal to the km/h formula at every speed (unit test); an 80 t,
  4-axle wagon gives 2,168 N at 100 km/h. (The research summary quoted "about 2,154 N" for
  that case; the 14 N difference is not explained and the formula itself is used.) Szanto's
  own fits give lower values for some heavy-haul stock: the coefficients stay configurable.
- **Shinkansen Series 100, ballasted track**: B. P. Rochard and F. Schmid, "A review of methods
  to measure and calculate train resistances", Proc. IMechE Part F 214 (2000) 185–199,
  DOI 10.1243/0954409001531306, eq. 24: R = 11.06 + 0.2232 v + 0.0156168 v² (kN, v m/s), i.e.
  A 11,060 N, B 223.2, C 15.6168, for the complete 886 t train (the paper's Table 1 says 856 t).
  Slab track (eq. 23) changes B to 109.44; Series 200 (eq. 25): 8,202 / 106.56 / 11.9232.
  **Checked only against a Scribd copy, not the publisher's version.**
- Not used: the original Davis coach table (old stock, from a simulator site) and the
  Armstrong–Swift / LABRADOR B formula (whose printed "MW" must be kW with 0.005, or MW
  with 5.0; and it does not suit trains with mostly powered axles).

## Traction

- **Siemens Vectron AC**: starting tractive effort 300 kN, maximum power **at the wheel**
  6,400 kW ([Siemens](https://www.mobility.siemens.com/global/en/portfolio/rolling-stock/locomotives/vectron/ac-dc-ms.html)).
  The force is capped by min(starting force, power / speed, adhesion on the driven axles).
  The preset's 90 t mass, resistance and brake force are **uncalibrated**.

## Braking

- **Passenger brake demands**: 3 %, 6 %, 9 %, 12 % of g for notch 1–3 and emergency
  (Alturbeh, Stow, Tucker, Lawton, "Modelling and Simulation of Train Brake System in Low
  Adhesion Conditions", [PDF](https://pure.hud.ac.uk/ws/portalfiles/portal/14343760/Modelling_and_Simulation_of_Train_Brake_System_in_Low_Adhesion_Conditions.pdf)).
  Demands, not achieved decelerations.
- **Freight, emergency, P position**: N. Bosso, M. Magelli, N. Zampieri, Railway Engineering
  Science 31 (2023) 162–171, DOI 10.1007/s40534-022-00296-9: a 1,689 t train (E402B + 20 × 80 t
  Shimmns), 74 % braked weight, UIC 544-1 stopping distance 739.3 m from 100 km/h, so a
  whole-stop equivalent of about 0.52 m/s². Their model's delays: application 1 s, propagation
  200 m/s, filling 5 s (95 % after 5 s). The game's wagon preset uses the 0.52 m/s² equivalent
  as its full brake **without** adding those delays (adding both would count the delay twice).
  **Ordinary freight service braking is not sourced** (uncalibrated).
- Brakes hold a train only up to their force and the adhesion of the braked axles; beyond that
  a train rolls back (unit-tested).

## Adhesion (wheel–rail friction)

- Dry 0.25–0.30, wet 0.18–0.20 (M.-S. Kim, Int. J. of Mechanics 9 (2015) 198–205, reported
  comparisons); a severe oxide/water paste as low as 0.02 (Tribology Letters 2021, laboratory).
  The game's presets: dry 0.25, wet 0.18, severe 0.02 (choices within those ranges).

## The trains' own data (`enginespec`), as read from the disc

Observed on the disc (2026-11-12): all 1,203 traincar configs have `mass` (kg) and an
`enginespec` KUID; the 187 enginespec assets have kind `engine` (163) or `steam-engine` (24).
Locomotive enginespecs carry `throttle-power`: one container per notch, each a list of
`speed force` pairs. Example, the SD40-2 (`<kuid:-12:3534>`, mass 190,508) with
`<kuid:-1:42004221>`: notch 1 = 0 → 165, 2.2 → 83, 4.4 → 55, 6.6 → 41, 8.8 → 33, 13.0 → 0;
notch 6 starts at 990. Wagon enginespecs (e.g. `<kuid:-1:42004201>`) have only `motor`.

How the port uses it (`src/content/EngineSpec`, `game::ownVehicleSpec`): speed in m/s and force
in kN, as the Trainz wiki documents (see [public-formats.md](public-formats.md)); the top
notch is the full-throttle curve, interpolated linearly, 0 past its end; curves of several
locomotives add up. **Unresolved:** read as kN, 990 kN at a standstill is about 2.5× a real
SD40-2's starting tractive effort (~400 kN), so either the unit or the game's use of the
curve differs; the adhesion limit (0.25 × the driven mass × g, a preset) decides the start
for now. The `motor` values (`max-accel`, `max-decel`, `brakeratio`, `resistance`, …) are read
but not used: their units are not established.

Notches: each `throttle-power` notch is its own curve (W/S step them in `route-viewer --drive`).
A consist whose locomotives have different notch counts gets as many notches as the one with
the most; the others follow at the same fraction of their own range (rounded), so all reach
the top together. That is the port's choice: how the game drives a mixed consist is not known.

## Brakes (2026-11-13)

**Dynamic brake** (from the train's own data): an `engine` enginespec's `dynamic-brake`
container has, like `throttle-power`, one curve per notch of `speed force` pairs, read as
m/s → kN. The SD40-2's notch 1 is 1.333 → 0, 2 → 30, 5 → 25, 10 → 15, 12 → 0. The port sums the
curves over the locomotives, with the same notch mapping as traction. The force is limited by
the grip of the driven axles, and there is no traction while it is on. In `route-viewer
--drive`, S below notch 0 steps into the dynamic-brake notches.

**Train brake handle** (the port's model, UNCALIBRATED): TS2010's `keyboard.txt` binds
`train_cabin_brake_application` (A), `_lap` (Z), `_release` (Q) and `_emergency` (Pause), the
controls of a self-lapping air brake. The port's `game::BrakeHandle` gives that feel without an
air-pressure model:

- Application builds the brake up at 15% a second;
- Lap holds it;
- Release lets it fall at 25% a second;
- Emergency is the full brake at once.

The full brake force is still the freight-wagon preset (0.52 m/s², an emergency figure). The
enginespec's `pressure/*`, `brakeratio` and `max-decel` keys are read but not used, because
their units are not established.

## Air brake (2026-11-13)

**From the files** (`content::EngineSpec`): the `pressure` container's `brakepipe` (charged),
`brakeinitial`, `brakefull` and `mainreservoir`, times its `scale`. Values on the disc's 187
enginespecs:

- brake pipe: 0.00736041 (85), 0.00595441 (45), 0.00665741 (14), a few others;
- main reservoir: 0.0081548 (96), 0.00946941 (56), …

The unit is not established. If 0.00946941 is a 90 psi main reservoir (a common US value), one
psi is ~1.052e-4. Then the SD40-2's pipe is 70 psi, its initial reduction 65.9 and its full
service 42.8, all typical figures. The port uses that only to show approximate psi; the model
itself works in the files' unit.

**The port's model** (`game::AirBrake`, uncalibrated rates):

- the handle moves an equalising reservoir: Application takes it to the initial reduction at
  once, then on to full service in about 7 s; Release recharges it in about 20 s; Lap holds it;
  Emergency vents it;
- the brake pipe follows with a lag of 1 s + 0.05 s per car;
- the brake demand is the fraction of the way from charged to full service.

The `flowsize` and `volume` containers (pipes, reservoirs, cylinders) are not used yet. AI
trains still set their brake directly.
