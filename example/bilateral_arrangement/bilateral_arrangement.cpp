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
#include <algorithm> /* std::random_shuffle, std::remove_if, std::find_if */
#include <iostream> /* std::endl */
#include <queue>
#include <tuple>
#include <vector>

#include "bilateral_arrangement.h"
#include "libcargo.h"

using namespace cargo;

const int BATCH = 30;

BilateralArrangement::BilateralArrangement()
    : RSAlgorithm("bilateral_arrangement", true), grid_(100) {
  this->batch_time() = BATCH;
  this->nswapped_ = 0;
}

void BilateralArrangement::match() {
  /* BilateralArrangement is a random algorithm due to this shuffle. */
  std::random_shuffle(customers().begin(), customers().end());

  while (!customers().empty()) {
    print << "Handling cust " << cust.id() << std::endl;
    Customer cust = customers().back();
    customers().pop_back();
    this->beg_ht();
    this->reset_workspace();
    this->candidates =
      this->grid_.within(pickup_range(cust), cust.orig());

    print << "\tGot " << this->candidates.size()
          << " candidates (range=" << pickup_range(cust) << ", s.avg=" << ss/this->candidates.size() << ")" << std::endl;

    DistInt best_cost = InfInt;

    for (const MutableVehicleSptr& cand : this->candidates) {
      // Speed-up heuristics:
      //   1) Try only if vehicle has capacity at this point in time
      //   2) Try only if vehicle's current schedule len < 8 customer stops
      // if (cand->capacity() > 1 && cand->schedule().data().size() < 10) {
      if (cand->schedule().data().size() < 10) {
        DistInt cost =
          sop_insert(*cand, cust, sch, rte) - cand->route().cost() + cand->next_node_distance();
        if (cost < 0) {
          print(MessageType::Error) << "Got negative detour!" << std::endl;
          print << cand->id() << std::endl;
          print << cost << " (" << new_cst << "-" << cand->route().cost() << ")" << std::endl;
          print << "Current schedule: ";
          for (const Stop& sp : cand->schedule().data())
            print << sp.loc() << " ";
          print << std::endl;
          print << "nnd: " << cand->next_node_distance() << std::endl;
          print << "New schedule: ";
          for (const Stop& sp : sch)
            print << sp.loc() << " ";
          print << std::endl;
          throw;
        }
        if (cost < best_cost) {
          // No constraints check
          best_vehl = cand;
          best_sch  = sch;
          best_rte  = rte;
          best_cost  = cost;
        }
      }
      if (this->timeout(this->timeout_0))
        break;
    }
    if (best_vehl != nullptr) {
      // Do constraints check
      if (chkcap(best_vehl->capacity(), best_sch)
       && chktw(best_sch, best_rte)) {
        matched = true;
      } else {
        print << "\tBest vehl " << best_vehl->id() << " infeasible! Trying replace..." << std::endl;
        CustId remove_me = randcust(best_vehl->schedule().data());
        if (remove_me != -1) {
          old_sch = best_vehl->schedule().data();
          sop_replace(best_vehl, remove_me, cust, best_sch, best_rte);
          if (chkcap(best_vehl->capacity(), best_sch)
           && chktw(best_sch, best_rte)) {
            print << "\t\tSucceeded replaced cust " << remove_me << std::endl;
            nswapped_++;
            matched = true;
            removed_cust = remove_me;
          } else {
            print << "\t\tStill not passing constraints after replace cust " << remove_me << std::endl;
            best_vehl->set_sch(old_sch);
          }
        } else {
          print << "\tCould not replace! (remove_me == -1)" << std::endl;
        }
      }
    }

    /* Attempt commit to db */
    if (matched) {
      print << "Matched " << cust.id() << " with " << best_vehl->id() << std::endl;
      std::vector<CustId> cdel = {};
      if (removed_cust != -1)
        cdel.push_back(removed_cust);
      this->assign_or_delay(
          {cust.id()}, cdel, best_rte, best_sch, *best_vehl);
    } else
      this->beg_delay(cust.id());

    this->end_ht();
  }
}

void BilateralArrangement::handle_vehicle(const cargo::Vehicle& vehl) {
  this->grid_.insert(vehl);
}

void BilateralArrangement::end() {
  print(MessageType::Info) << "swaps: " << this->nswapped_ << std::endl;
  this->print_statistics();
}

void BilateralArrangement::listen(bool skip_assigned, bool skip_delayed) {
  this->grid_.clear();
  RSAlgorithm::listen(
    skip_assigned, skip_delayed);
}

void BilateralArrangement::reset_workspace() {
  this->sch = this->best_sch = this->old_sch = {};
  this->rte = this->best_rte = {};
  this->candidates = {};
  this->matched = false;
  this->best_vehl = nullptr;
  this->timeout_0 = hiclock::now();
  this->removed_cust = -1;
}

int main() {
  /* Set options */
  Options option;
  option.path_to_roadnet  = "../../data/roadnetwork/bj5.rnet";
  option.path_to_gtree    = "../../data/roadnetwork/bj5.gtree";
  option.path_to_edges    = "../../data/roadnetwork/bj5.edges";
  option.path_to_problem  = "../../data/benchmark/rs-md-7.instance";
  option.path_to_solution = "bilateral_arrangement.sol";
  option.path_to_dataout  = "bilateral_arrangement.dat";
  option.time_multiplier  = 1;
  option.vehicle_speed    = 20;
  option.matching_period  = 60;
  option.static_mode = true;
  Cargo cargo(option);
  BilateralArrangement ba;
  cargo.start(ba);

  return 0;
}

