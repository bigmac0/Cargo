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
#include <algorithm> /* iter_swap, random_shuffle */
#include <iostream>
#include <iterator>
#include <memory> /* shared_ptr */
#include <mutex> /* lock_guard */

#include "libcargo/cargo.h" /* Cargo::gtree() */
#include "libcargo/classes.h"
#include "libcargo/debug.h"
#include "libcargo/distance.h"
#include "libcargo/functions.h"
#include "libcargo/types.h"

#include "gtree/gtree.h"

namespace cargo {

/* Print ---------------------------------------------------------------------*/
void print_rte(const vec_t<Wayp>& rte) {
  for (const auto& wp : rte)
    std::cout << " (" << wp.first << "|" << wp.second << ")";
  std::cout << std::endl;
}

void print_sch(const vec_t<Stop>& sch) {
  for (const auto& sp : sch)
    std::cout << " (" << sp.owner() << "|" << sp.loc() << "|" << sp.early()
              << "|" << sp.late() << "|" << (int)sp.type() << ")";
  std::cout << std::endl;
}


/* Random customer -----------------------------------------------------------*/
CustId randcust(const vec_t<Stop>& sch) {
  vec_t<Stop> s = sch; // make a copy
  std::random_shuffle(s.begin(), s.end());  // randomize the order
  for (auto i = s.begin(); i != s.end()-1; ++i)
    if (i->type() != StopType::VehlOrig && i->type() != StopType::VehlDest)
      for (auto j = i+1; j != s.end(); ++j)
        if (j->owner() == i->owner())
            return i->owner();
  return -1;
}


/* Pickup range --------------------------------------------------------------*/
// TODO: This function assumes the same vehicle speed for all vehicles. In the
// future, change it to accept specific speeds.
DistInt pickup_range(const Customer& cust) {
  return Cargo::vspeed() * cust.late() - Cargo::basecost(cust.id()) -
         Cargo::vspeed() * Cargo::now();
}


/* Route operations ----------------------------------------------------------*/
DistInt route_through(const vec_t<Stop>& sch, vec_t<Wayp>& rteout,
                      GTree::G_Tree& gtree) {
  DistInt cst = 0;
  rteout.clear();
  rteout.push_back({0, sch.front().loc()});

  // if ((sch.front().loc() == sch.back().loc()) && sch.size() == 2) {
  //   rteout.push_back({cst, sch.back().loc()});
  // }

  for (SchIdx i = 0; i < sch.size()-1; ++i) {
    const NodeId& from = sch.at(i).loc();
    const NodeId& to = sch.at(i+1).loc();

    if (from == to) {
      rteout.push_back({cst, to});
      continue;
    }

    vec_t<NodeId> seg {};
    bool in_cache = false;
    { std::lock_guard<std::mutex> splock(Cargo::spmx); // Lock acquired
    in_cache = Cargo::spexist(from, to);
    if (in_cache) {
      seg = Cargo::spget(from, to);
      // std::cout << "CACHE HIT" << std::endl;
    }
    } // Lock released
    if(!in_cache) {
      // std::cout << "CACHE MISS" << std::endl;
      try { gtree.find_path(from, to, seg); }
      catch (...) {
        std::cout << "gtree.find_path(" << from << "," << to << ") failed" << std::endl;
        print_sch(sch);
        std::cout << "index: " << i << std::endl;
        throw;
      }
      std::lock_guard<std::mutex> splock(Cargo::spmx); // Lock acquired
      Cargo::spput(from, to, seg);
    } // Lock released
    for (size_t i = 1; i < seg.size(); ++i) {
      cst += Cargo::edgew(seg.at(i-1), seg.at(i));
      rteout.push_back({cst, seg.at(i)});
    }
  }
  return cst;
}

DistInt route_through(const vec_t<Stop> & sch,
                            vec_t<Wayp> & rteout) {
  return route_through(sch, rteout, Cargo::gtree());
}

bool chkpc(const Schedule& s) {
  // Last stop must be this vehicle destination
  if (s.data().back().type() != StopType::VehlDest ||
      s.data().back().owner() != s.owner()) {
    DEBUG(3, { std::cout << "chk_prec() last stop is not a vehl dest, or last stop owner mismatch" << std::endl; });
    return false;
  }

  // A vehicle origin that is not this vehicle cannot appear at 0
  if (s.data().begin()->type() == StopType::VehlOrig &&
      s.data().begin()->owner() != s.owner()) {
    DEBUG(3, { std::cout << "chk_prec() first stop is not this VehlOrig" << std::endl; });
    return false;
  }

  return chkpc(s.data());
}

bool chkpc(const vec_t<Stop>& sch) {

  // Second-to-last stop cannot be any origin if schedule size > 2
  if (sch.size() > 2 &&
      (std::prev(sch.end(), 2)->type() == StopType::CustOrig ||
       std::prev(sch.end(), 2)->type() == StopType::VehlOrig)) {
    DEBUG(3, { std::cout << "chk_prec() 2nd-last stop is an origin" << std::endl; });
    return false;
  }

  // In the worst case, this algorithm walks the schedule for each stop in
  // order to find the paired requests. The complexity is O(|schedule|^2).
  bool paired;
  for (auto i = sch.begin(); i != sch.end(); ++i) {
    // Any vehicle origin cannot appear in the schedule, aside from at 0
    if (i > sch.begin() && i->type() == StopType::VehlOrig) {
      DEBUG(3, { std::cout << "chk_prec() VehlOrig in interior" << std::endl; });
      return false;
    }

    paired = false;
    for (auto j = sch.begin(); j != sch.end() && !paired; ++j) {
      if (i == j) continue;
      if (i->owner() == j->owner()) {
        if (i->type() == StopType::CustOrig &&
            j->type() == StopType::CustDest && i < j)
          paired = true;
        else if (i->type() == StopType::CustOrig &&
                 j->type() == StopType::CustDest && i > j) {
          DEBUG(3, { std::cout << "CustOrig appears after its CustDest" << std::endl; });
          return false;
        } else if (i->type() == StopType::CustDest &&
                   j->type() == StopType::CustOrig && i > j)
          paired = true;
        else if (i->type() == StopType::CustDest &&
                 j->type() == StopType::CustOrig && i < j) {
          DEBUG(3, { std::cout << "CustDest appears before its CustOrig" << std::endl; });
          return false;
        } else if (i->type() == StopType::VehlOrig &&
                   j->type() == StopType::VehlDest && i < j)
          paired = true;
        else if (i->type() == StopType::VehlOrig &&
                 j->type() == StopType::VehlDest && i > j) {
          DEBUG(3, { std::cout << "VehlOrig appears after its VehlDest" << std::endl; });
          return false;
        } else if (i->type() == StopType::VehlDest &&
                   j->type() == StopType::VehlOrig && i > j)
          paired = true;
        else if (i->type() == StopType::VehlDest &&
                 j->type() == StopType::VehlOrig && i < j) {
          DEBUG(3, { std::cout << "VehlDest appears before its VehlOrig" << std::endl; });
          return false;
        }
      }
    }
    if (!paired && (i->type() != StopType::CustDest &&
                    i->type() != StopType::VehlDest)) {
      DEBUG(3, { std::cout << "An origin is unpaired" << std::endl; });
      return false;
    }
  }
  return true;
}

bool chktw(const vec_t<Stop>& sch, const vec_t<Wayp>& rte) {
  DEBUG(3, { std::cout << "chktw() got sch:"; print_sch(sch); });
  DEBUG(3, { std::cout << "chktw() got rte:"; print_rte(rte); });

  DistInt remaining_distance = (rte.back().first - rte.front().first);
  float remaining_time = remaining_distance/(float)Cargo::vspeed();
  float arrival_time = remaining_time + Cargo::now();

  // Check the end point first
  if (sch.back().late() != -1 &&
      sch.back().late() < arrival_time) {
    DEBUG(3, { std::cout << "chktw() found "
      << "sch.back().late(): " << sch.back().late()
      << "; remaining_distance: " << remaining_distance
      << "; remaining_time: " << remaining_time
      << "; current_time: " << Cargo::now()
      << "; arrival_time: " << arrival_time
      << std::endl;
    });
    return false;
  }

  // Walk along the schedule and the route. O(|schedule|+|route|)
  auto j = rte.begin();
  for (auto i = sch.begin(); i != sch.end(); ++i) {
    while (j->second != i->loc()) {
      ++j;
      if (j == rte.end()) {
        std::cout << "chktw reached end before schedule" << std::endl;
        print_sch(sch);
        print_rte(rte);
        throw;
      }
    }
    float eta = (j->first-rte.front().first)/(float)Cargo::vspeed() + Cargo::now();
    if (i->late() != -1 && i->late() < eta) {
      DEBUG(3, { std::cout << "chktw() found "
        << "i->late(): " << i->late()
        << "; j->first: " << j->first
        << "; rte.front().first: " << rte.front().first
        << "; speed: " << Cargo::vspeed()
        << "; current time: " << Cargo::now()
        << "; eta: " << eta
        << std::endl;
      });
      return false;
    }
  }
  return true;
}

bool chkcap(const Load& capacity, const vec_t<Stop>& sch) {
  int q = capacity;  // REMAINING capacity
  for (const Stop& stop : sch) {
    if (stop.type() == StopType::CustOrig) q -= 1;  // TODO: Replace 1 with customer's Load
    if (stop.type() == StopType::CustDest) q += 1;
    if (q < 0) {
      DEBUG(3, { std::cout << "chkcap failed (" << capacity << "): ";
        print_sch(sch);
      });
      return false;
    }
  }
  return true;
}


/* Schedule operations -------------------------------------------------------*/
void opdel(vec_t<Stop>& sch, const CustId& cust_id) {
  // Special case: removed cust is last stop for a TAXI
  bool last_customer_stop = (sch.size() > 2 ? sch.at(sch.size()-2).owner() == cust_id : false);
  bool is_taxi = (sch.back().late() == -1);

  vec_t<Stop> old_sch = sch;
  opdel_any(sch, cust_id);
  if (old_sch.size() - sch.size() != 2) {
    std::cout << "opdel unknown error" << std::endl;
    std::cout << "Schedule: ";
    print_sch(old_sch);
    std::cout << "To remove: " << cust_id << std::endl;
    throw;
  }

  if (last_customer_stop && is_taxi) {
    vec_t<Stop> new_sch = {};
    const Stop& last_stop = sch.at(sch.size()-2);
    Stop new_dest(sch.front().owner(), last_stop.loc(), StopType::VehlDest, last_stop.early(), -1, -1);
    for (size_t i = 0; i < sch.size()-1; ++i)
      new_sch.push_back(sch.at(i));
    new_sch.push_back(new_dest);
    sch = new_sch;
  }
}

void opdel_any(vec_t<Stop>& sch, const CustId& cust_id) {
  vec_t<Stop> new_sch {};
  for (const Stop& a : sch)
    if (a.owner() != cust_id)
      new_sch.push_back(a);
  sch = new_sch;
}

DistInt sop_insert(const vec_t<Stop>& sch, const Stop& orig,
                       const Stop& dest, bool fix_start, bool fix_end,
                       vec_t<Stop>& schout, vec_t<Wayp>& rteout,
                       GTree::G_Tree& gtree) {
  DistInt mincst = InfInt;
  schout.clear();
  rteout.clear();

  vec_t<Stop> mutsch = sch;   // mutable schedule
  vec_t<Wayp> mutrte;         // mutable route

  auto check = [&](DistInt cst) {
    if (cst <= mincst) {
      mincst = cst;
      schout = mutsch;
      rteout = mutrte;
    }
  };

  mutsch.insert(mutsch.begin() + fix_start, orig);
  mutsch.insert(mutsch.begin() + fix_start, dest);

  // This algorithm uses a series of swaps to generate all insertion
  // combinations.  Here is an example of inserting stops (A, B) into a
  // 3-stop sched:
  // A B - - -
  // A - B - -
  // A - - B -
  // A - - - B
  // - A - - B
  // - A - B -
  // - A B - -
  // - - A B -
  // - - A - B
  // - - - A B
  int inc = 1;
  bool rst = false;
  vec_t<Stop>::iterator beg, end;
  for (auto i = mutsch.begin() + fix_start; i != mutsch.end() - 1 - fix_end;
       ++i) {
    beg = (inc == 1) ? i : mutsch.end() - 1 - fix_end;
    end = (inc == 1) ? mutsch.end() - 1 - fix_end : i + 1;
    for (auto j = beg; j != end; j += inc) {
      if (rst) {
        std::iter_swap(i - 1, i + 1);  // <-- O(1)
        rst = false;
      } else
        std::iter_swap(j, j + inc);
      check(route_through(mutsch, mutrte, gtree));
    }
    std::iter_swap(i, i + 1);
    if (inc == 1 && i < mutsch.end() - 2 - fix_end) {
      check(route_through(mutsch, mutrte, gtree));
    }
    if ((inc = -inc) == 1) rst = true;
  }
  return mincst;
}

DistInt sop_insert(const vec_t<Stop>& sch, const Stop& orig,
                       const Stop& dest, bool fix_start, bool fix_end,
                       vec_t<Stop>& schout,
                       vec_t<Wayp>& rteout) {
  return sop_insert(sch, orig, dest, fix_start, fix_end, schout, rteout,
                    Cargo::gtree());
}

DistInt sop_insert(const Vehicle& vehl, const Customer& cust,
                       vec_t<Stop>& schout, vec_t<Wayp>& rteout,
                       GTree::G_Tree& gtree) {
  // The distances to the nodes in the routes found by route_through need
  // to be corrected.  veh.schedule() passed here contains only un-visited
  // stops. The first stop in the schedule is the vehicle's next node
  // (because of step()).  route_through will give this stop a distance of 0.
  // The distances to other stops in the augmented schedule passed to
  // route_through will be relative to this first stop. The already-traveled
  // distance TO THIS FIRST STOP (the next node) should be added.
  DistInt head = vehl.route().data().at(vehl.idx_last_visited_node()+1).first;

  Stop cust_o(cust.id(), cust.orig(), StopType::CustOrig, cust.early(), cust.late());
  Stop cust_d(cust.id(), cust.dest(), StopType::CustDest, cust.early(), cust.late());

  DistInt mincst = 0;

  // If vehl is a taxi, it's last stop is NOT fixed.
  if (vehl.late() == -1) {
    vec_t<Stop> schin = vehl.schedule().data();
    schin.pop_back();  // remove the fake destination
    mincst = sop_insert(schin, cust_o, cust_d, true, false, schout, rteout, gtree);
    Stop last = schout.back();
    Stop fake_dest(vehl.id(), last.loc(), StopType::VehlDest, last.early(), -1, -1);
    schout.push_back(fake_dest);  // add a fake destination
  } else {
    mincst = sop_insert(vehl.schedule().data(), cust_o, cust_d, true, true, schout, rteout, gtree);
  }

  DEBUG(3, {
    std::cout << "Before insert " << cust.id() << " into " << vehl.id() << ": " << std::endl;
    print_rte(vehl.route().data());
    std::cout << "After insert " << cust.id() << " into " << vehl.id() << ":" << std::endl;
    print_rte(rteout);
    std::cout << "head: " << head << std::endl;
  });

  // Add head to the new nodes in the route
  for (auto& wp : rteout)
    wp.first += head;

  DEBUG(3, {
    std::cout << "After adding head: " << std::endl;
    print_rte(rteout);
  });

  // rteout.insert(rteout.begin(), vehl.route().at(vehl.idx_last_visited_node()));

  DEBUG(3, {
    // std::cout << "After adding curloc:" << std::endl;
    // print_rte(rteout);
    std::cout << "Returning cost: " << mincst+head << std::endl;
  });

  return mincst+head;
}

DistInt sop_insert(const Vehicle& vehl, const Customer& cust,
                       vec_t<Stop>& schout,
                       vec_t<Wayp>& rteout) {
  return sop_insert(vehl, cust, schout, rteout, Cargo::gtree());
}

DistInt sop_insert(const std::shared_ptr<MutableVehicle>& mutvehl,
                       const Customer& cust, vec_t<Stop>& schout,
                       vec_t<Wayp>& rteout) {
  return sop_insert(*mutvehl, cust, schout, rteout);
}

DistInt sop_replace(const MutableVehicle& mutvehl,
                    const CustId& rm, const Customer& cust,
                    vec_t<Stop>& schout, vec_t<Wayp>& rteout) {
  MutableVehicle mutcopy = mutvehl;
  vec_t<Stop> sch1 = mutcopy.schedule().data();
  opdel(sch1, rm);
  mutcopy.set_sch(sch1);
  return sop_insert(mutcopy, cust, schout, rteout);
}

DistInt sop_replace(const std::shared_ptr<MutableVehicle>& mutvehl,
                    const CustId& rm, const Customer& cust,
                    vec_t<Stop>& schout, vec_t<Wayp>& rteout) {
  return sop_replace(*mutvehl, rm, cust, schout, rteout);
}

}  // namespace cargo

