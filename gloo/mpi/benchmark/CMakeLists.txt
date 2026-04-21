set(GLOO_MPI_BENCH_COMMON_SRCS
  "${CMAKE_CURRENT_SOURCE_DIR}/common.cc"
  )

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

if(GLOO_HAVE_TRANSPORT_MYELON)
  add_executable(mpi_bench_myelon_pingpong
    "${CMAKE_CURRENT_SOURCE_DIR}/myelon_pingpong.cc"
    ${GLOO_MPI_BENCH_COMMON_SRCS}
    )
  target_link_libraries(mpi_bench_myelon_pingpong gloo)
else()
  message(
    STATUS
    "Skipping mpi_bench_myelon_pingpong: configure with -DUSE_MYELON=ON and a valid MYELON_PLAYGROUND_ROOT"
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
