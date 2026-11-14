// Driving a session's train on its real route (read from the original files, read-only):
// where the train stands, and drawing its own vehicle and bogie models along the track.
#pragma once

#include "content/EngineSpec.h"
#include "content/SessionFile.h"
#include "content/PlacedObject.h"
#include "content/SignalAsset.h"
#include "game/Drive.h"
#include "sim/Signals.h"
#include "sim/TrainPhysics.h"
#include "sim/TrackNetwork.h"
#include "view/AssetMeshes.h"
#include "view/SceneRender.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <optional>
#include <vector>

namespace trainz::game {

// The first vehicle's place on the track, facing the way the train runs: away from its second
// vehicle (vehicles are stored front to back; with one vehicle, toward the piece's `to` end).
// Found from the stored positions (not the stored trackPiece/direction, whose meaning is not
// fully known). nullopt if the first vehicle is more than maxDistance from any track.
// A vehicle's physics from its own files: `massKg` is the traincar's `mass`, `engine` its
// enginespec (null if unknown). Resistance is AAR RP-548 for that mass and the enginespec's
// axle count (4 when not given) and the brake the freight-wagon emergency equivalent, as in
// sim::freightWagon (neither is read from the files yet). A vehicle whose enginespec has
// throttle-power curves is driven, with its highest notch as the full-throttle curve and every
// notch's curve in notchCurvesN, and its `dynamic-brake` notches in dynamicBrakeCurvesN (in N).
// Without a usable mass it falls back to the 40 t wagon preset (and says so in `source`).
// The route's signals (sgOb objects with a track attachment on a piece of `net`), each for the
// direction of travel its attachment gives, named by the name in its record when it has one.
std::vector<trainz::sim::Signal> routeSignals(const trainz::sim::TrackNetwork& net,
                                              const std::vector<trainz::content::PlacedObject>& objects);

// Turns each signal object to face along its track: yaw = the heading of its direction of
// travel - 90 degrees, i.e. the model's +y along that direction (as for vehicles), so lamps
// on the model's -y side face the trains coming toward it. A GUESS from the disc's signal
// models (their a.light points sit at y < 0), not traced; the stored yaw of signals is 0.
// Returns how many were turned.
std::size_t orientSignals(const trainz::sim::TrackNetwork& net, std::vector<trainz::content::PlacedObject>& objects);

// The lit lamps of a signal object drawn with `mesh` at its position and yaw, for the state
// `stateForAspect` picks: world positions of its "a.light<N>" points and their corona colours.
// An asset without a `signals` list uses its a.red / a.orange (a.yellow) / a.green point.
struct SignalLamp {
    double x = 0, y = 0, z = 0;
    std::array<std::uint8_t, 3> rgb{255, 255, 255};
};
std::vector<SignalLamp> signalLamps(const trainz::content::PlacedObject& object, const trainz::view::PreparedMesh& mesh,
                                    const trainz::content::SignalLook& look, int aspect);

// Points a train takes on the track: from its front `lead` back over `length` metres, one
// every `step` metres and at both ends.
std::vector<trainz::sim::Occupied> trainOccupies(const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& lead,
                                                 double length, double step = 5.0);

// Points taken by a session's standing trains (each vehicle's stored position, if it lies
// within 5 m of a piece), leaving out train number `except` (0-based; -1 for none).
std::vector<trainz::sim::Occupied> standingTrains(const trainz::sim::TrackNetwork& net,
                                                  const std::vector<trainz::content::Train>& trains, long except);

trainz::sim::VehicleSpec ownVehicleSpec(std::optional<double> massKg, const trainz::content::EngineSpec* engine);

std::optional<trainz::sim::TrackPosition> trainStart(const trainz::sim::TrackNetwork& net, const trainz::content::Train& train,
                                                     double maxDistance = 5.0);

struct RouteVehicle {
    const trainz::view::PreparedMesh* body = nullptr;
    std::array<const trainz::view::PreparedMesh*, 2> bogies = {nullptr, nullptr};  // on a.bog0, a.bog1 (may be null)
    std::array<double, 2> bogieY = {0, 0};  // the points' positions along the body (+y forward)
    bool onBogies = false;                  // both points known
    double length = 14;                     // along the body, from its model
    bool flipped = false;                   // turned round in its train (its +y points backwards)
};

// A vehicle from its model: its length (the model's extent along y) and bogie points.
RouteVehicle routeVehicle(const trainz::view::PreparedMesh& body, const trainz::view::PreparedMesh* bogie0,
                          const trainz::view::PreparedMesh* bogie1);

// Draws the vehicles along the track, the first one's middle at `front`, each following the
// one before (`gap` metres apart). Returns how many were drawn.
int addRouteTrain(trainz::view::Scene& scene, const trainz::sim::TrackNetwork& net, const trainz::sim::TrackPosition& front,
                  const std::vector<RouteVehicle>& vehicles, double gap = 0.5);

struct DriverCommand {
    trainz::content::Kuid command;
    std::map<std::string, std::string> properties;
    // What the port makes of it (set by the caller from the command asset's name):
    // "navigate-to-trackmark", "navigate-via-trackmark", "wait-for", "wait-for-trigger", or "" (not followed).
    std::string kind;
};

// A session train ready to run: its place and physics (a Drive), its drawn vehicles and length.
struct RouteTrain {
    std::size_t index = 0;  // in the session's trains
    Drive drive;
    std::vector<RouteVehicle> vehicles;  // the ones with a model the port can read
    double length = 0;                   // metres, the drawn vehicles with 0.5 m gaps
    std::size_t vehicleCount = 0, withOwnMass = 0, powered = 0;
    // Every car front to back, with its physics and (if it has one the port can read) its model;
    // `vehicles`, `length` and `drive.spec` are worked out from these (refreshTrain).
    struct Car {
        trainz::sim::VehicleSpec spec;
        std::optional<RouteVehicle> vehicle;
    };
    std::vector<Car> cars;
    // The train's air brake, with the pressures of its first car that has them (the locomotive).
    AirBrake airBrake;
    bool parked = false;  // cars left behind by uncoupling: they stand (the AI does not drive them)
    bool fallbackPower = false;  // explicit train-level driving aid, never part of a car's real data
    std::optional<std::size_t> heldRedSignal;
    // A place to drive to and stop at (from the session's driver orders), and its name.
    std::optional<trainz::sim::TrackPosition> destination;
    std::string destinationName;
    bool destinationVia = false;      // drive through it rather than stop at it
    // Its session orders and where it is in them (see runOrders).
    std::vector<DriverCommand> orders;
    std::size_t orderIndex = 0;
    double orderClock = 0;            // seconds waited in the current order
    bool waitingForOrder = false;     // stand still: the current order is a wait
    std::string orderNote;            // what it is doing, for display  // reached its braking margin; hold even after an overrun until it clears
};
// Builds train `index` of the session: placed where it stands (trainStart), each vehicle's own
// mass and enginespec (ownVehicleSpec; the preset locomotive pulls when none has curves).
// nullopt if it does not stand on track or no vehicle has a model.
std::optional<RouteTrain> makeRouteTrain(const trainz::sim::TrackNetwork& net, const std::vector<trainz::content::Train>& trains,
                                         std::size_t index, trainz::view::AssetMeshes& meshes);

// A simple AI driver (the port's own, not the game's): runs up to `cruise` m/s on its throttle
// notches and brakes to stop `margin` metres short of the nearest of: a signal at stop ahead,
// a point another train takes, or a line end; at caution it holds half the cruise speed. Its
// braking assumes `decel` m/s^2 (the full brake is the freight preset's 0.52, an emergency
// figure, so it plans on less). UNCALIBRATED.
struct AiDriver {
    double cruise = 30 / 3.6;
    double margin = 20;
    double decel = 0.35;
    // Sets d.drive.controls for the next step. `others`: points taken by every other train.
    void control(RouteTrain& d, const trainz::sim::TrackNetwork& net, const trainz::sim::SignalSystem* signals,
                 const std::vector<trainz::sim::Occupied>& others) const;
    // The distance (metres) to the nearest place it must stop before, within `look` metres, or
    // nullopt if none; and why ("signal", "train", "line end").
    struct StopAhead {
        double distance = 0;
        const char* why = "";
    };
    std::optional<StopAhead> stopAhead(const RouteTrain& d, const trainz::sim::TrackNetwork& net,
                                       const trainz::sim::SignalSystem* signals, const std::vector<trainz::sim::Occupied>& others,
                                       double look = 1500) const;
};

// Works out `vehicles`, `length` and `drive.spec` from `cars` (the place, speed and controls
// stay). Only an explicitly enabled train-level fallback may supply missing traction.
void refreshTrain(RouteTrain& t);

// A train's lead is its first vehicle's MIDDLE (as session positions are). Its two buffer ends,
// and its last vehicle's middle, each facing the way the train faces:
trainz::sim::TrackPosition trainFrontEnd(const trainz::sim::TrackNetwork& net, const RouteTrain& t);
trainz::sim::TrackPosition trainRearEnd(const trainz::sim::TrackNetwork& net, const RouteTrain& t);
trainz::sim::TrackPosition lastVehicleMiddle(const trainz::sim::TrackNetwork& net, const RouteTrain& t);
std::vector<trainz::sim::Occupied> trainOccupies(const trainz::sim::TrackNetwork& net, const RouteTrain& t, double step = 5.0);

// Another train's end within `within` metres of the selected moving end, along the track.
struct Contact {
    std::size_t other = 0;     // index into the others
    bool atOurFront = true;    // our front (lead) or our rear
    bool theirFront = false;   // their front or their rear
    double gap = 0;            // metres
};
std::optional<Contact> findContact(const trainz::sim::TrackNetwork& net, const RouteTrain& me, const std::vector<RouteTrain>& others,
                                   double within = 1.0, bool atOurFront = true);

// Couples `other` onto `me` at the contact: its cars join ours in one line (turned round when
// they face the other way), `me`'s lead moves to the new front if they join at our front, and
// the speed is shared by mass. `other` is left empty (no cars); the caller removes it.
void coupleTrains(const trainz::sim::TrackNetwork& net, RouteTrain& me, RouteTrain& other, const Contact& contact);

// Uncouples the rearmost `count` cars of `me` (at least one car stays) as a new standing train
// just behind the rest. nullopt if there is nothing to uncouple.
std::optional<RouteTrain> uncoupleRear(const trainz::sim::TrackNetwork& net, RouteTrain& me, std::size_t count = 1);

// Named trackmarks on the track: tsO2 objects with a name and a track attachment (the same
// 23-byte tail as signals; observed), by name.
std::map<std::string, trainz::sim::TrackPosition> routeTrackmarks(const trainz::sim::TrackNetwork& net,
                                                                  const std::vector<trainz::content::PlacedObject>& objects);

// A session's driver orders from its "Driver Setup" rules (observed settings): for each driver,
// the name of the vehicle it drives and its commands in order, each a drivercommand asset KUID
// and its properties (e.g. trackmarkname 'B Stop').
// The kind of a drivercommand asset by its username (e.g. "Navigate To Trackmark"); "" if the
// port does not follow it.
std::string commandKind(const std::string& username);
struct DriverOrders {
    std::string vehicle;
    std::vector<DriverCommand> commands;
};
std::vector<DriverOrders> driverOrders(const std::vector<trainz::content::Behaviour>& rules);

// A train held at a red signal (heldRedSignal) that only it holds red - it overran into the block
// - would wait forever. Releases the hold when the signal would not be at stop with `others` (the
// points every OTHER train takes; the signals' junction settings as they are) and returns true;
// keeps it (false) while anything else is in the block, or when not held.
bool releaseIfOnlySelfHeld(RouteTrain& t, const trainz::sim::SignalSystem& signals, const std::vector<trainz::sim::Occupied>& others);

// Works through a train's session orders, one step of `seconds` (the port's own reading of
// the commands; the game's command scripts are not run):
//  - navigate to / via a trackmark: route there (junction settings applied to `net`), then
//    stop at it (to) or pass it (via), and go on to the next order;
//  - wait for: stand `timetowait` seconds (assumed to be seconds);
//  - wait for trigger: stand until the named train's front is within 30 m of the named trigger;
//  - anything else: skipped (noted).
// `marks` holds trackmarks and triggers by name; `trainFront(vehicleName)` the front of the train
// with that vehicle, if any.
void runOrders(RouteTrain& t, trainz::sim::TrackNetwork& net, const std::map<std::string, trainz::sim::TrackPosition>& marks,
               const std::function<std::optional<trainz::sim::TrackPosition>(const std::string&)>& trainFront, double seconds);

}  // namespace trainz::game
