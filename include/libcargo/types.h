// MIT License
//
// Copyright (c) 2018 the Cargo authors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#ifndef CARGO_INCLUDE_TYPES_H_
#define CARGO_INCLUDE_TYPES_H_

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <limits>

namespace cargo {

// We use many "logical" numerical types such as node IDs, edge IDs,
// trip IDs, etc. Unfortunately the possibility exists for these types to
// get "mingled" in the code. For example, consider, where TripId and NodeId
// are int types:
//     TripId tid;
//     NodeId nid;
//
// The following assignment is allowed by C++ even though the two vars are
// logically different:
//     nid = tid;
//     tid = nid;
//
// There are other similar issues in type conversion.
//
// Google or-tools has a nice template class in int_type.h to prevent these
// kinds of issues. We can consider using their template IntType class in the
// future. But for now, using typedefs to at least provide some semantic
// difference is better than nothing.

// ints are guaranteed at least 32-bits ~> 2 billion values.
typedef int NodeId;
typedef int EdgeId;
typedef int TripId;

// Double to minimize rounding errors; unit is meters.
typedef double Distance;

// No need for double precision because these will never be operated on. Float
// gives us 7 decimal digits. For lng/lat coordinates, the 6th digit corresponds
// to a precision of roughly 110 centimeters(!), so 7 is more than enough.
typedef float Longitude;
typedef float Latitude;

// Spatial data type.
typedef struct {
    Longitude lng;
    Latitude lat;
} Point;

// Nodes in the road network.
struct Node {
    NodeId id;
    Point coordinates;
};

// Weighted edge in the road network.
// TODO: Not used?
struct Edge {
    EdgeId id;
    NodeId from_id;
    NodeId to_id;
    Distance weight;
};

// A lookup table keyed by node id.
typedef std::unordered_map<NodeId, Node> NodeMap;

// A lookup table for edges.  The table is "undirected"; that is, from-to and
// to-from key combinations both exist in the table. Usage:
//     EdgeMap em_;
//     em[from_id][to_id] = weight;
typedef std::unordered_map<NodeId, std::unordered_map<NodeId, Distance>> EdgeMap;

// An ordered sequence of nodes.
typedef std::vector<Node> Route;

// Used as the internal simulation time; one SimTime is roughly equivalent to
// one real second. Time windows are expressed as SimTime, with 0 being the
// start of the simulation. Travel time is also expressed as SimTime, computed
// as the real (haversine) distance divided by the real speed, in m/s.
typedef int SimTime;

// Demand d > 0 indicates customer; otherwise vehicle with capacity d
typedef int Demand;

// All customers and vehicles are represented as raw "trips". The difference
// between them is mostly semantic. The one logical difference is that
// vehicles have negative demand, representing their capacity in the real
// world.
struct Trip {
    TripId id;
    NodeId oid;
    NodeId did;

    // The time window is expressed as SimTimes. early tells the simulator when
    // to broadcast this trip. late tells the solver the constraint for when
    // the trip should arrive at destination.
    SimTime early;
    SimTime late;

    // Positive demand corresponds to a customer request; negative demand
    // corresponds to vehicle capacity.
    Demand demand;
};

// Purely semantic definitions
typedef Trip Customer;
typedef Trip Vehicle;

// A set of trips is a TripGroup. In case order is important, TripGroup is
// represented as a vector.
typedef std::vector<Trip> TripGroup;

// Stop types
enum class StopType : int {
    CUSTOMER_ORIGIN,
    CUSTOMER_DESTINATION,
    VEHICLE_ORIGIN,
    VEHICLE_DESTINATION,
};

// A stop corresponds to one single trip.
struct Stop {
    TripId trip_id;
    NodeId destination;
    StopType type;
};

// An ordered sequence of stops. There can be consecutive stops with the same
// destination, but different trip_ids.
typedef std::vector<Stop> Schedule;

// A problem instance is the set of trips keyed by their early time. When the
// simulator time reaches SimTime, all the trips in the group are broadcasted.
// Map is used here to sort the trip groups by simtime. Then, we can easily
// find the largest trip.early to set the minimum simulation time.
struct ProblemInstance {
    std::string name;
    std::map<SimTime, TripGroup> trips;
};

// Simulator status flags
// TODO: These might not be necessary?
enum class SimulatorStatus : int {
    // Default state of the simulator.
    RUNNING,

    // The simulator reaches the FINISHED state if two conditions are met:
    // (1) all trips from the problem instance have been broadcasted,
    // (2) all vehicles have arrived at their destinations.
    FINISHED,
};

// Vehicle speed, in m/s
typedef float Speed;

// Filepath
typedef std::string Filepath;

// Infinity
const double kInfinity = std::numeric_limits<double>::infinity();

// Math PI
const double kPI = 3.141592653589793238462643383279502884L;

} // namespace cargo

#endif // CARGO_INCLUDE_TYPES_H_
