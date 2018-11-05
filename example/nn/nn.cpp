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
#include <iostream>
#include <queue>

#include "libcargo.h"
#include "nn.h"

using namespace cargo;

const int BATCH = 30;                       // batch time, seconds

auto cmp = [](rank_cand left, rank_cand right) {
  return std::get<0>(left) > std::get<0>(right);
};

NearestNeighbor::NearestNeighbor()
    : RSAlgorithm("nn", true), grid_(100) {
  this->batch_time() = BATCH;               // (rsalgorithm.h)
}

void NearestNeighbor::handle_customer(const Customer& cust) {
  this->beg_ht();                           // begin timing (rsalgorithm.h)
  this->reset_workspace();                  // reset workspace variables
  this->candidates =                        // collect candidates
    this->grid_.within(pickup_range(cust), cust.orig()); // (functions.h, grid.h)

  /* Rank candidates (timeout) */
  std::priority_queue<rank_cand, vec_t<rank_cand>, decltype(cmp)>
    my_q(cmp);                              // rank by nearest
  for (const MutableVehicleSptr& cand : this->candidates) {
    DistDbl cost = haversine(cand->last_visited_node(), cust.orig());
    rank_cand rc = std::make_pair(cost, cand);
    my_q.push(rc);
    if(this->timeout(this->timeout_0))      // (rsalgorithm.h)
      break;
  }

  /* Accept nearest valid */
  while (!my_q.empty() && !matched) {
    rank_cand rc = my_q.top();              // access order by nearest
    my_q.pop();                             // remove from queue
    best_vehl = std::get<1>(rc);
    sop_insert(best_vehl, cust, sch, rte);  // (functions.h)
    if (chktw(sch, rte)                     // check time window (functions.h)
     && chkcap(best_vehl->capacity(), sch)) // check capacity (functions.h)
      matched = true;                       // accept
    if (this->timeout(this->timeout_0))     // (rsalgorithm.h)
      break;
  }

  /* Attempt commit to db */
  if (matched) {
    this->assign_or_delay(                  // (rsalgorithm.h)
        {cust.id()}, {}, rte, sch, *best_vehl);
    /* Update local grid copy of the vehicle */
    best_vehl->set_rte(rte);                // (classes.h)
    best_vehl->set_sch(sch);
    best_vehl->reset_lvn();
  } else
    this->beg_delay(cust.id());             // (rsalgorithm.h)

  this->end_ht();                           // end timing
}

void NearestNeighbor::handle_vehicle(const Vehicle& vehl) {
  this->grid_.insert(vehl);                 // (grid.h)
}

void NearestNeighbor::end() {
  this->print_statistics();                 // (rsalgorithm.h)
}

void NearestNeighbor::listen(bool skip_assigned, bool skip_delayed) {
  this->grid_.clear();                      // (grid.h)
  RSAlgorithm::listen(                      // (rsalgorithm.h)
    skip_assigned, skip_delayed);
}

void NearestNeighbor::reset_workspace() {
  this->sch = {};
  this->rte = {};
  this->candidates = {};
  this->matched = false;
  this->best_vehl = nullptr;
  this->timeout_0 = hiclock::now();
}

int main() {
  Options option;                           // (options.h)
  option.path_to_roadnet  = "../../data/roadnetwork/bj5.rnet";
  option.path_to_gtree    = "../../data/roadnetwork/bj5.gtree";
  option.path_to_edges    = "../../data/roadnetwork/bj5.edges";
  option.path_to_problem  = "../../data/benchmark/rs-m1k-c1.instance";
  option.path_to_solution = "nn.sol";
  option.path_to_dataout  = "nn.dat";
  option.time_multiplier  = 1;
  option.vehicle_speed    = 10;
  option.matching_period  = 60;
  option.strict_mode = false;
  option.static_mode = false;
  Cargo cargo(option);                      // (cargo.h)
  NearestNeighbor nn;
  cargo.start(nn);

  return 0;
}

