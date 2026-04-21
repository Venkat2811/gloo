set(GLOO_MPI_BENCH_COMMON_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/common.cc"
  )

set(
  MYELON_PLAYGROUND_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../myelon-playground"
  CACHE PATH
  "Path to a myelon-playground checkout that contains crates/myelon-gloo-ffi"
  )

set(MYELON_GLOO_FFI_INCLUDE_DIR "${MYELON_PLAYGROUND_ROOT}/crates/myelon-gloo-ffi/include")
set(MYELON_GLOO_FFI_LIB "${MYELON_PLAYGROUND_ROOT}/target/release/libmyelon_gloo_ffi.a")

add_executable(mpi_bench_allreduce
  "${CMAKE_CURRENT_SOURCE_DIR}/allreduce.cc"
  ${GLOO_MPI_BENCH_COMMON_SRCS}
  )
target_link_libraries(mpi_bench_allreduce gloo)

add_executable(mpi_bench_broadcast
  "${CMAKE_CURRENT_SOURCE_DIR}/broadcast.cc"
  ${GLOO_MPI_BENCH_COMMON_SRCS}
  )
target_link_libraries(mpi_bench_broadcast gloo)

add_executable(mpi_bench_pingpong
  "${CMAKE_CURRENT_SOURCE_DIR}/pingpong.cc"
  ${GLOO_MPI_BENCH_COMMON_SRCS}
  )
target_link_libraries(mpi_bench_pingpong gloo)

if(
  EXISTS "${MYELON_PLAYGROUND_ROOT}/Cargo.toml"
  AND EXISTS "${MYELON_GLOO_FFI_INCLUDE_DIR}/myelon_gloo_ffi.h"
  )
  find_program(CARGO_EXECUTABLE cargo REQUIRED)

  add_custom_command(
    OUTPUT "${MYELON_GLOO_FFI_LIB}"
    COMMAND "${CARGO_EXECUTABLE}" build -p myelon-gloo-ffi --release
    WORKING_DIRECTORY "${MYELON_PLAYGROUND_ROOT}"
    COMMENT "Building myelon-gloo-ffi Rust bindings"
    VERBATIM
    )

  add_custom_target(myelon_gloo_ffi_build
    DEPENDS "${MYELON_GLOO_FFI_LIB}"
    )

  add_executable(mpi_bench_myelon_pingpong
    "${CMAKE_CURRENT_SOURCE_DIR}/myelon_pingpong.cc"
    ${GLOO_MPI_BENCH_COMMON_SRCS}
    )
  target_include_directories(mpi_bench_myelon_pingpong PRIVATE "${MYELON_GLOO_FFI_INCLUDE_DIR}")
  target_link_libraries(mpi_bench_myelon_pingpong gloo "${MYELON_GLOO_FFI_LIB}")
  add_dependencies(mpi_bench_myelon_pingpong myelon_gloo_ffi_build)
else()
  message(
    STATUS
    "Skipping mpi_bench_myelon_pingpong: set MYELON_PLAYGROUND_ROOT to a myelon-playground checkout containing crates/myelon-gloo-ffi"
    )
endif()

if(GLOO_INSTALL)
  set(GLOO_MPI_BENCH_INSTALL_TARGETS
    mpi_bench_allreduce
    mpi_bench_broadcast
    mpi_bench_pingpong
    )
  if(TARGET mpi_bench_myelon_pingpong)
    list(APPEND GLOO_MPI_BENCH_INSTALL_TARGETS mpi_bench_myelon_pingpong)
  endif()
  install(
    TARGETS ${GLOO_MPI_BENCH_INSTALL_TARGETS}
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    )
endif()
