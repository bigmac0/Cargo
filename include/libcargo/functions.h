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
#ifndef CARGO_INCLUDE_LIBCARGO_FUNCTIONS_H_
#define CARGO_INCLUDE_LIBCARGO_FUNCTIONS_H_
#include <memory> /* shared_ptr */

#include "cargo.h"
#include "classes.h"
#include "types.h"

#include "../gtree/gtree.h"

namespace cargo {

void print_rte(const std::vector<Wayp> &);
void print_sch(const std::vector<Stop> &);

DistInt pickup_range(const Customer &, const SimlTime &, GTree::G_Tree &);
DistInt pickup_range(const Customer &, const SimlTime &);

// Given a schedule, return the route through the schedule and its cost. Cost is
// integer because G_Tree only returns int.
// O(|schedule|*|nodes|)
DistInt route_through(const std::vector<Stop> &, std::vector<Wayp> &, GTree::G_Tree &);
DistInt route_through(const std::vector<Stop> &, std::vector<Wayp> &);

// Given a schedule, find if precedence is satisfied.
// O(|schedule|^2)
bool chkpc(const Schedule &);

// Given a schedule, find if time windows are satisfied
// O(|schedule|+|route|)
bool chktw(const std::vector<Stop> &, const std::vector<Wayp> &);

// Pick a random customer from a vehicle's schedule
// (only customers with both stops in the schedule remaining can be selected;
// returns -1 if no eligible customer)
CustId randcust(const std::vector<Stop> &);

// Remove a customer from a vehicle's schedule
// (only performs removal if both customer stops are in the schedule. otherwise
// returns false)
void remove_cust(std::vector<Stop> &, const CustId &);

// Given a schedule and a customer, return the cost of the best-insertion
// schedule, and output the schedule and the route. The two bools are for
// fixing the end points. Set the first bool to true to fix the start, and
// set the second bool to true to fix the end.
// O(|schedule|^2*c_route_through)
DistInt sop_insert(const std::vector<Stop> &, const Stop &, const Stop &, bool, bool,
                   std::vector<Stop> &, std::vector<Wayp> &, GTree::G_Tree &);
DistInt sop_insert(const std::vector<Stop> &, const Stop &, const Stop &, bool, bool,
                   std::vector<Stop> &, std::vector<Wayp> &);

/* This version corrects the distances in rteout (param4) by taking into
 * account vehicle's (param1) traveled distance. */
DistInt sop_insert(const Vehicle &, const Customer &,
                   std::vector<Stop> &, std::vector<Wayp> &, GTree::G_Tree &);
DistInt sop_insert(const Vehicle &, const Customer &,
                   std::vector<Stop> &, std::vector<Wayp> &);

/* This version is for convenience; vehicles returned by grid are
 * MutableVehicle pointers. */
DistInt sop_insert(const std::shared_ptr<MutableVehicle> &, const Customer &,
                   std::vector<Stop> &, std::vector<Wayp> &);


/* Replace customer (param2) with a new customer (param3)
 * (remove the old customer, then sop_insert the new customer) */
DistInt sop_replace(const std::shared_ptr<MutableVehicle> &, const CustId &,
                    const Customer &, std::vector<Stop> &, std::vector<Wayp> &);

}  // namespace cargo

#endif  // CARGO_INCLUDE_LIBCARGO_FUNCTIONS_H_

