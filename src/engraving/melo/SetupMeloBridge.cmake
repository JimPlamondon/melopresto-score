# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# JiMStaff Milestone 1 — build and link the melo-musescore-bridge Rust
# staticlib from an explicit JiMS Kernel checkout. Fails clearly when the
# checkout, cargo, or the built ABI is missing; never vendors Rust build
# outputs into this repository.

function(setup_melo_bridge target)
    # Preserve the selected checkout across automatic CMake reconfiguration.
    # The environment supplies the initial default; -DMELO_ROOT changes an
    # existing build explicitly.
    if (NOT DEFINED MELO_ROOT)
        set(MELO_ROOT "$ENV{MELO_ROOT}")
        if (NOT MELO_ROOT)
            set(MELO_ROOT "/Users/jim/Developer/MeloPresto/GitHub/melopresto")
        endif()
    endif()
    set(MELO_ROOT "${MELO_ROOT}" CACHE PATH "MeloPresto Kernel checkout used by this build")
    set(MELO_WORKSPACE "${MELO_ROOT}/Libraries/melo")
    set(MELO_BRIDGE_CRATE "${MELO_WORKSPACE}/crates/melo-musescore-bridge")

    if (NOT EXISTS "${MELO_BRIDGE_CRATE}/Cargo.toml")
        message(FATAL_ERROR
                "MeloPresto bridge crate not found at ${MELO_BRIDGE_CRATE}. "
                "Configure with -DMELO_ROOT=<path> to select a MeloPresto Kernel checkout containing "
                "crates/melo-musescore-bridge.")
    endif()

    find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{HOME}/.cargo/bin")
    if (NOT CARGO_EXECUTABLE)
        message(FATAL_ERROR "cargo not found; install Rust to build the MeloPresto bridge.")
    endif()

    set(MELO_BRIDGE_LIB "${MELO_WORKSPACE}/target/release/libmelo_musescore_bridge.a")
    execute_process(
        COMMAND ${CARGO_EXECUTABLE} build --locked --release -p melo-musescore-bridge
        WORKING_DIRECTORY ${MELO_WORKSPACE}
        RESULT_VARIABLE MELO_CARGO_RESULT
        OUTPUT_VARIABLE MELO_CARGO_OUT
        ERROR_VARIABLE MELO_CARGO_ERR)
    if (NOT MELO_CARGO_RESULT EQUAL 0)
        message(FATAL_ERROR "cargo build of melo-musescore-bridge failed:\n${MELO_CARGO_ERR}")
    endif()
    if (NOT EXISTS "${MELO_BRIDGE_LIB}")
        message(FATAL_ERROR "expected staticlib missing after build: ${MELO_BRIDGE_LIB}")
    endif()

    add_library(melo_musescore_bridge STATIC IMPORTED GLOBAL)
    # Cargo checks its complete dependency graph on every Score build. A
    # configure-only invocation leaves the imported archive stale after Rust
    # source edits, even when MELO_ROOT still selects the correct checkout.
    add_custom_target(melo_bridge_build
        COMMAND ${CARGO_EXECUTABLE} build --locked --release -p melo-musescore-bridge
        WORKING_DIRECTORY "${MELO_WORKSPACE}"
        BYPRODUCTS "${MELO_BRIDGE_LIB}"
        COMMENT "Checking the selected MeloPresto Kernel bridge"
        VERBATIM)
    add_dependencies(melo_musescore_bridge melo_bridge_build)
    add_dependencies(${target} melo_bridge_build)
    set_target_properties(melo_musescore_bridge PROPERTIES
                          IMPORTED_LOCATION "${MELO_BRIDGE_LIB}")
    target_include_directories(${target} PRIVATE "${MELO_BRIDGE_CRATE}/include")
    target_link_libraries(${target} PRIVATE melo_musescore_bridge)
    message(STATUS "MeloPresto bridge linked from ${MELO_BRIDGE_LIB}")
endfunction()
