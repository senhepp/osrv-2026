cmake_minimum_required(VERSION 3.16)
project(CryptoThreads LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Путь к pthreads-win32
set(PTHREADS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/pthreads)

#pthread
add_library(pthread STATIC
    ${PTHREADS_ROOT}/pthread.c
    ${PTHREADS_ROOT}/sched.c
    ${PTHREADS_ROOT}/misc.c
    ${PTHREADS_ROOT}/barrier.c
)

target_include_directories(pthread PUBLIC ${PTHREADS_ROOT})

target_compile_definitions(pthread PUBLIC 
    PTW32_STATIC_LIB
    HAVE_STRUCT_TIMESPEC
    __MINGW_USE_VC2005_COMPAT 
    _POSIX_THREADS
    $<$<CONFIG:Debug>:PTW32_BUILD>
)

if(MINGW)
    target_compile_options(pthread PRIVATE -static-libgcc)
endif()

add_executable(main main.cpp)

target_link_libraries(main PRIVATE 
    pthread
    -static-libgcc -static-libstdc++  # Статическая линковка для standalone EXE
)

target_include_directories(main PRIVATE ${PTHREADS_ROOT})

if(WIN32 AND MINGW)
    target_link_options(main PRIVATE -static)
endif()
