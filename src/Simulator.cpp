#include "Simulator.h"

#include <chrono>
#include <thread>
#include <iterator>
#include <vector>

#include "base/basic_types.h"
#include "base/ridesharing_types.h"
#include "base/file.h"
#include "base/options.h"
#include "gtree/GTree.h"

namespace cargo {

using opts::Options;
using file::ReadNodes;
using file::ReadEdges;
using file::ReadProblemInstance;

// --------------------------------------------------------
// Simulator
// --------------------------------------------------------

Simulator::Simulator()
    : status_(SimulatorStatus::RUNNING), t_(0), count_active_(0) {}

void Simulator::SetOptions(Options opts) {
    opts_ = opts;
}

void Simulator::Initialize() {
    // Loads the road network and the problem instance.
    ReadNodes(opts_.RoadNetworkPath(), nodes_);
    ReadEdges(opts_.EdgePath(), edges_);
    ReadProblemInstance(opts_.ProblemInstancePath(), pi_);

    // Sets the gtree.
    GTree::load(opts_.GTreePath());
    gtree_ = GTree::get();

    // Sets the minimim simulation time to ensure all trips will be broadcasted.
    tmin_ = pi_.trips.rbegin()->first;

    // Sets the sleep time based on the time scale option.
    sleep_ = std::round((float)1000/opts_.SimTimeScale());
}

void Simulator::InsertVehicle(const Trip &trip) {
    // (1) Inserts the vehicle's route into the routes_ table.
    // Uses GTree to find the vehicle's initial route from origin to
    // destination. The route returned by GTree is a vector of ints
    // corresponding to node ID's; the distance is an int summing the edge
    // lengths. So, the vector is walked in order to find the corresponding
    // Nodes.
    std::vector<int> rt_raw;
    gtree_.find_path(trip.oid, trip.did, rt_raw);
    Route rt;
    for (auto id : rt_raw)
        rt.push_back(nodes_.at(id));
    routes_[trip.id] = rt;

    // (2) Inserts the vehicle's schedule into the schedules_ table.
    // The initial schedule is the vehicle's origin and destination.
    Schedule sch;
    sch.push_back({trip.id, trip.oid, StopType::VEHICLE_ORIGIN});
    sch.push_back({trip.id, trip.did, StopType::VEHICLE_DESTINATION});
    schedules_[trip.id] = sch;

    // (3) Inserts the vehicle's position into the positions_ table.
    // The initial position is the head of the route.
    positions_[trip.id] = rt.begin();

    // (4) Inserts the vehicle's residual into the residuals_ table.  Computes
    // the distance to the next node in the route. nx is guaranteed to exist
    // because the trip must have at least two nodes, origin and destination.
    auto nx = std::next(rt.begin());
    residuals_[trip.id] = edges_.at(trip.oid).at(nx->id);

    // (5) Inserts the vehicle's capacity into the capacities_ table.
    // The capacity is equal to the trip demand.
    capacities_[trip.id] = trip.demand;

    // (6) Increment the count of active vehicles.
    count_active_++;
}

void Simulator::AdvanceSimulationState() {
    // Move the existing vehicles
    // Loop through residuals_, and update them based on how far each
    // vehicle has moved. If the residual becomes negative, a vehicle has
    // arrived at its next node; update the position and check
    //     (1) does the node belong to a stop in the vehicle's schedule?
    //         if so, update the capacity if it is a DESTINATION type
    //     (2) is the node the VEHICLE_DESTINATION?
    //         if so, decrement active vehicles
    for (const auto &kv : residuals_) {
        TripId tid = kv.first;
        Distance res = kv.second;

        // Only move vehicles where next position is not route.end()
        if (std::next(positions_.at(tid)) != routes_.at(tid).end()) {
            // speed is m/s; each t_ corresponds to 1 real second
            res -= opts_.VehicleSpeed();
            if (res <= 0) {
                positions_.at(tid)++;
                auto nx = std::next(positions_.at(tid));
                residuals_.at(tid) = edges_.at(positions_.at(tid)->id).at(nx->id);
                // TODO: check if is a stop
            }
        }
    }
}

void Simulator::UpdateRoute(const TripId &tid, const Route) {

}

void Simulator::UpdateSchedule(const TripId &tid, const Schedule) {

}

void Simulator::UpdatePosition(const TripId &tid, Route::const_iterator x) {

}

void Simulator::UpdateResidual(const TripId &tid, const Distance d) {

}

void Simulator::UpdateCapacity(const TripId &tid, const Demand q) {

}

void Simulator::Run() {
    // This process will run (and block anything downstream) until two
    // conditions are met: t_ > tmin_, and no more active vehicles.
    while (true) {
        // Start the clock for this interval
        // auto t_start = std::chrono::high_resolution_clock::now();

        if (t_ > 0)
            AdvanceSimulationState();

        // All trips broadcasted, and no more active vehicles?
        if (t_ > tmin_ && count_active_ == 0)
            break;

        // Broadcast new trips
        // Loop through the TripGroup corresponding to the current SimTime t_
        // and broadcast based on whether the trip is a customer or vehicle
        if (pi_.trips.find(t_) != pi_.trips.end()) {
            for (auto trip : pi_.trips[t_]) {
                if (trip.demand < 0) {
                    InsertVehicle(trip);
                    // TODO: broadcast a new vehicle
                } else {
                    // TODO: customer_online() embark
                }
            }
        }

        // update vehicle status
        // auto t_end = std::chrono::high_resolution_clock::now();
        // auto elapsed = std::round(std::chrono::duration<double, std::milli>(t_end - t_start).count());
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_));

        // Advance the simulation time
        t_++;
    }
}

} // namespace cargo