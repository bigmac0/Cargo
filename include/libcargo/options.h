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
#ifndef CARGO_INCLUDE_LIBCARGO_OPTIONS_H_
#define CARGO_INCLUDE_LIBCARGO_OPTIONS_H_

#include "types.h"

namespace cargo {

struct Options {
    Filepath path_to_roadnet;
    Filepath path_to_gtree;
    Filepath path_to_edges;
    Filepath path_to_problem;

    // A multiplier for the ratio between the SimTime and real time. A
    // multiplier of 2, for example, will set one SimTime to be equal to
    // approximately 1/2 real seconds.
    float time_multiplier;

    Speed vehicle_speed; // meters per second
    int gps_frequency; // GPS "update" rate
};

} // namespace cargo

#endif // CARGO_INCLUDE_LIBCARGO_OPTIONS_H_
