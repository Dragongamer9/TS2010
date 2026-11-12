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
