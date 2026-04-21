if(USE_MPI)
  set(GLOO_MPI_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/context.cc"
    )
  set(GLOO_MPI_HDRS
    "${CMAKE_CURRENT_SOURCE_DIR}/context.h"
    )
  list(APPEND GLOO_SRCS ${GLOO_MPI_SRCS})
  list(APPEND GLOO_HDRS ${GLOO_MPI_HDRS})

  if(BUILD_EXAMPLES)
    add_subdirectory(example)
  endif()

  if(BUILD_BENCHMARK)
    add_subdirectory(benchmark)
  endif()
endif()

set(GLOO_SRCS ${GLOO_SRCS} PARENT_SCOPE)
set(GLOO_HDRS ${GLOO_HDRS} PARENT_SCOPE)
