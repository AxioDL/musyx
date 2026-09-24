cmake_minimum_required(VERSION 3.21)
project(MusyX)

set(MUSY_TARGET MUSY_TARGET_PC)

option(MUSYX_ENABLE_SDL "Build the optional SDL device adapter" ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_library(musyx STATIC
        src/musyx/runtime/seq.c
        src/musyx/runtime/synth.c
        src/musyx/runtime/seq_api.c
        src/musyx/runtime/snd_synthapi.c
        src/musyx/runtime/stream.c
        src/musyx/runtime/synthdata.c
        src/musyx/runtime/synthmacros.c
        src/musyx/runtime/synthvoice.c
        src/musyx/runtime/synth_ac.c
        src/musyx/runtime/synth_adsr.c
        src/musyx/runtime/synth_vsamples.c
        src/musyx/runtime/synth_dbtab.c
        src/musyx/runtime/s_data.c
        src/musyx/runtime/hw_dspctrl.c
        src/musyx/runtime/hw_volconv.c
        src/musyx/runtime/snd3d.c
        src/musyx/runtime/snd_init.c
        src/musyx/runtime/snd_math.c
        src/musyx/runtime/snd_midictrl.c
        src/musyx/runtime/snd_service.c
        src/musyx/runtime/hardware.c
        src/musyx/runtime/hw_aramdma.c
        src/musyx/runtime/hw_dolphin.c
        src/musyx/runtime/hw_pc_output.c
        src/musyx/runtime/hw_pc_rate.c
        src/musyx/runtime/hw_pc_vsample.c
        src/musyx/runtime/hw_pc_song.c
        src/musyx/runtime/hw_pc_samples.c
        src/musyx/runtime/hw_pc_assets.c
        src/musyx/runtime/hw_pc_decode.c
        src/musyx/runtime/hw_pc.c
        src/musyx/runtime/hw_memory.c
        src/musyx/runtime/hw_lib_dolphin.c
        src/musyx/runtime/CheapReverb/creverb_fx.c
        src/musyx/runtime/CheapReverb/creverb.c
        src/musyx/runtime/StdReverb/reverb_fx.c
        src/musyx/runtime/StdReverb/reverb.c
        src/musyx/runtime/Delay/delay_fx.c
        src/musyx/runtime/Chorus/chorus_fx.c
)

target_include_directories(musyx PUBLIC include)
target_compile_definitions(musyx PUBLIC
        MUSY_TARGET=${MUSY_TARGET}
        MUSY_VERSION_MAJOR=2
        MUSY_VERSION_MINOR=0
        MUSY_VERSION_PATCH=0
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
        $<$<CONFIG:RelWithDebInfo>:_DEBUG>
        $<$<CONFIG:MinSizeRel>:NDEBUG>)
set_target_properties(musyx PROPERTIES DEBUG_POSTFIX D)
set_target_properties(musyx PROPERTIES RELWITHDEBINFO_POSTFIX RD)
set_target_properties(musyx PROPERTIES MINSIZEREL_POSTFIX MIN)

if (MUSYX_ENABLE_SDL)
    if (NOT TARGET SDL3::SDL3)
        find_package(SDL3 REQUIRED)
    endif ()
    target_sources(musyx PRIVATE src/musyx/runtime/hw_pc_sdl.c)
    target_link_libraries(musyx PRIVATE SDL3::SDL3)
else ()
    target_sources(musyx PRIVATE src/musyx/runtime/hw_pc_null.c)
endif ()
if (NOT MSVC)
    target_link_libraries(musyx PUBLIC m)
endif ()
