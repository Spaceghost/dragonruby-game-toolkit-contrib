cmake_minimum_required(VERSION 3.24)
project(DragonRubyRivalBpfCapsule LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(BpfCapsule CONFIG REQUIRED)

set(RIVAL_SRC "${CMAKE_CURRENT_LIST_DIR}/../../src")

bpf_capsule_object(
    rival_bpf
    OUTPUT rival.bpf.o
    SOURCES
        rival_bpf.c
        "${RIVAL_SRC}/competitive.c"
    INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_LIST_DIR}"
        "${RIVAL_SRC}"
    COMPILE_OPTIONS -g
    LINK_OPTIONS ${BPF_CAPSULE_LINK_OPTIONS}
)

bpf_capsule_skeleton(
    rival_skeleton
    OUTPUT rival.skel.h
    OBJECT "${rival_bpf}"
    NAME rival
)

add_executable(rival_capsule_host rival_host.c)
target_include_directories(rival_capsule_host PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(rival_capsule_host PRIVATE BpfCapsule::host rival_skeleton)
