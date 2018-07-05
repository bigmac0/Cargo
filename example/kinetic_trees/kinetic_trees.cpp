// Copyright (c) 2018 the Cargo authors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include <algorithm> /* std::find_if */
#include <exception>
#include <iostream> /* std::endl */
#include <thread>
#include <vector>

#include "libcargo.h"

#include "kinetic_trees.h"
#include "treeTaxiPath.h"

KineticTrees::KineticTrees()
    : RSAlgorithm("gkt"),
      grid_(100)  /* <-- Initialize my 100x100 grid (see grid.h) */ {
  batch_time() = 1;  // Set batch to 1 second
  nmat_ = 0;      // Initialize my private counter
}

void KineticTrees::handle_customer(const cargo::Customer& cust) {
  /* Don't consider customers that are assigned but not yet picked up */
  if (cust.assigned()) return;

  /* Containers for storing outputs */
  cargo::DistInt cst, best_cst = cargo::InfInt;
  std::vector<cargo::Stop> sch, best_sch;
  std::vector<cargo::Wayp> rte, best_rte;

  /* best_vehl will point to an underlying MutableVehicle in our grid */
  std::shared_ptr<cargo::MutableVehicle> best_vehl = nullptr;
  bool matched = false;

  /* Get candidates from the local grid index
   * (the grid is refreshed during listen()) */
  cargo::DistInt rng = cargo::pickup_range(cust, cargo::Cargo::now());
  auto candidates = grid_.within_about(rng, cust.orig());

  /* Create the customer stops to insert into best vehicle */
  Stop cust_orig = Stop(cust.id(), cust.orig(), StopType::CustOrig, cust.early(), cust.late());
  Stop cust_dest = Stop(cust.id(), cust.dest(), StopType::CustDest, cust.early(), cust.late());

  /* Loop through candidates and check which is the greedy match */
  for (const auto& cand : candidates) {
    if (cand->queued() == cand->capacity())
      continue;  // don't consider vehs already queued to capacity

    /* Use vehicle's next waypoint to construct augmented schedule */
    Stop vehl_curr = cand->schedule().data().front();

    /* Define the maximum travel bound based on customer's time window */
    DistInt max_travel = (cust.late()-cust.early())*Cargo::vspeed();

    /* Find the least-cost schedule using kinetic tree */
    cst = kt_.at(cand->id())->value(vehl_curr.loc(), cust.orig(), cust.dest(), rng, max_travel);
    if (cst == -1) continue; // <-- vehicle is infeasible for this cust
    std::vector<std::pair<NodeId, bool>> schseq;
    kt_.at(cand->id())->printTempStopSequence(schseq);

    /* Convert the sequence of nodes returned from the tree into a vector of
     * stops for commiting into the db */
    sch.clear();
    sch.push_back(vehl_curr);
    for (auto i = schseq.begin()+1; i != schseq.end(); ++i) {
      auto j = std::find_if(cand->schedule().data().begin()+1, cand->schedule().data().end(),
        [&](const Stop& a) {
          bool same_loc = (a.loc() == i->first);
          bool same_type;
          if ((a.type() == StopType::VehlOrig || a.type() == StopType::CustOrig)
            && i->second == true) same_type = true;
          else if ((a.type() == StopType::VehlDest || a.type() == StopType::CustDest)
            && i->second == false) same_type = true;
          else
            same_type = false;
          return (same_loc && same_type);
        });
      if (j != cand->schedule().data().end()) sch.push_back(*j);
      else {
        if (i->first == cust_orig.loc()) sch.push_back(cust_orig);
        if (i->first == cust_dest.loc()) sch.push_back(cust_dest);
      }
    }

    /* Route through the least-cost schedule */
    // The distances to the nodes in the routes found by route_through need to
    // be corrected.  sch passed here contains only un-visited stops. The first
    // stop in the schedule is the vehicle's next node.  route_through will
    // give this stop a distance of 0.  The distances to other stops in the
    // augmented schedule passed to route_through will be relative to this
    // first stop. The already-traveled distance (the head) should be added.
    DistInt head = cand->route().dist_at(cand->idx_last_visited_node() + 1);
    rte.clear();
    route_through(sch, rte);
    // Add head to the new nodes in the route
    for (auto& wp : rte) wp.first += head;
    rte.insert(rte.begin(), cand->route().at(cand->idx_last_visited_node()));

    bool within_time = cargo::check_timewindow_constr(sch, rte);
    if ((cst < best_cst) && within_time) {
      if (best_vehl != nullptr)
        kt_.at(best_vehl->id())->cancel(); // <-- cancel current best
      best_cst = cst;
      best_sch = sch;
      best_rte = rte;
      best_vehl = cand;  // copy the pointer
      matched = true;
    } else
      kt_.at(cand->id())->cancel();
  }

  /* Commit match to the db. Also refresh our local grid index, so data is
   * fresh for other handle_customers that occur before the next listen(). */
  if (matched) {
    std::vector<cargo::Wayp> sync_rte;
    std::vector<cargo::Stop> sync_sch;
    cargo::DistInt sync_nnd;

    if (commit({cust}, best_vehl, best_rte, best_sch, sync_rte, sync_sch, sync_nnd)) {
      grid_.commit(best_vehl, sync_rte, sync_sch, sync_nnd);
      kt_.at(best_vehl->id())->push(); // DO I HAVE TO PUSH THE SYNC_SCH?
      print_success << "Match (cust" << cust.id() << ", veh" << best_vehl->id() << ")\n";
      nmat_++;
    } else {
      print_out << "commit refused!!" << std::endl;
    }
  }
}

void KineticTrees::handle_vehicle(const cargo::Vehicle& vehl) {
  /* Insert into grid */
  grid_.insert(vehl);

  /* Create kinetic tree */
  if (kt_.count(vehl.id()) == 0)
    kt_[vehl.id()] = new TreeTaxiPath(vehl.orig(), vehl.dest());

  /* Add to last-modified */
  if (last_modified_.count(vehl.id()) == 0)
    last_modified_[vehl.id()] = Cargo::now();

  /* Step the kinetic tree */
  if (sched_.count(vehl.id()) != 0)
  /* The vehicle could have moved a lot since the last handle_vehicle to it.
   * We check its current schedule against our memory of its schedule and
   * adjust the kinetic tree to match. */
  if (sched_.at(vehl.id()).at(0).loc() != vehl.dest()) {
  std::vector<Stop> visited_stops;
  for (auto j = sched_.at(vehl.id()).begin()+1; j != sched_.at(vehl.id()).end(); ++j) {
    auto i = std::find_if(vehl.schedule().data().begin(), vehl.schedule().data().end(),
      [&](const Stop& a) { return a == *j; });
    if (i == vehl.schedule().data().end()) // <-- no longer in vehl's sch
      visited_stops.push_back(*j);
    else break;
  }
  for (size_t i = 0; i < visited_stops.size(); ++i)
    kt_.at(vehl.id())->step();
  }

  /* Create/Update vehicle schedules */
  sched_[vehl.id()] = vehl.schedule().data();

  /* Update kinetic tree node times */
  int dur = Cargo::now() - last_modified_.at(vehl.id());
  if (dur > 0) kt_.at(vehl.id())->moved(dur * Cargo::vspeed());

  /* Update last modified */
  last_modified_[vehl.id()] = Cargo::now();
}

void KineticTrees::end() {
  print_success << "Matches: " << nmat_ << std::endl;  // Print a msg

  /* Cleanup */
  for (auto kv : kt_) delete kv.second;
}

void KineticTrees::listen() {
  grid_.clear();          // Clear the index...
  RSAlgorithm::listen();  // ...then call listen()
}

int main() {
  /* Set the options */
  cargo::Options op;
  op.path_to_roadnet  = "../../data/roadnetwork/mny.rnet";
  op.path_to_gtree    = "../../data/roadnetwork/mny.gtree";
  op.path_to_edges    = "../../data/roadnetwork/mny.edges";
  op.path_to_problem  = "../../data/benchmark/rs-sm-4.instance";
  op.path_to_solution = "a.sol";
  op.time_multiplier  = 5;
  op.vehicle_speed    = 10;
  op.matching_period  = 60;

  cargo::Cargo cargo(op);

  /* Initialize a new kt */
  KineticTrees kt;

  /* Start Cargo */
  cargo.start(kt);
}

