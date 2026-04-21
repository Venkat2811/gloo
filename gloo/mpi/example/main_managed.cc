/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cassert>
#include <iostream>

#include "gloo/allreduce.h"
#include "gloo/config.h"
#include "gloo/math.h"
#include "gloo/mpi/context.h"

#if GLOO_HAVE_TRANSPORT_TCP
#include "gloo/transport/tcp/device.h"
#endif

#if GLOO_HAVE_TRANSPORT_UV
#include "gloo/transport/uv/device.h"
#endif

namespace {

std::shared_ptr<gloo::transport::Device> createDevice() {
#if GLOO_HAVE_TRANSPORT_TCP
  return gloo::transport::tcp::CreateDevice("localhost");
#elif GLOO_HAVE_TRANSPORT_UV
  return gloo::transport::uv::CreateDevice("localhost");
#else
#error "MPI examples require either the TCP or UV transport."
#endif
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
  auto dev = createDevice();

  // Create Gloo context and delegate management of MPI_Init/MPI_Finalize
  auto context = gloo::mpi::Context::createManaged();
  context->connectFullMesh(dev);

  // Run a simple unbound-buffer allreduce so this example works with
  // transports like UV that do not implement the legacy bound-buffer API.
  int input = context->rank;
  int output = 0;
  gloo::AllreduceOptions opts(context);
  opts.setInput(&input, 1);
  opts.setOutput(&output, 1);
  opts.setReduceFunction(
      static_cast<void (*)(void*, const void*, const void*, size_t)>(
          &gloo::sum<int>));
  gloo::allreduce(opts);
  std::cout << "Result: " << output << std::endl;

  return 0;
}
