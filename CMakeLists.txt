cmake_minimum_required(VERSION 3.20)
project(q2pt C)

if(NOT WIN32)
	message(FATAL_ERROR "Only the Windows build is set up so far")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(Q2_ARCH x64)
else()
	set(Q2_ARCH x86)
endif()

# Game data lives in run/baseq2. The exe and its ref_*.dll must share a
# directory: run/ for x64, run/x86/ for the Win32 fallback (start that one
# from run/x86 with "+set basedir ..").
set(Q2_DATA ${CMAKE_SOURCE_DIR}/run)
if(Q2_ARCH STREQUAL x64)
	set(Q2_OUT ${Q2_DATA})
else()
	set(Q2_OUT ${Q2_DATA}/x86)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} ${Q2_OUT})
endforeach()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


# C_ONLY: the x86 assembly paths are never built, on either architecture
add_compile_definitions(WIN32 _WINDOWS C_ONLY _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE _WINSOCK_DEPRECATED_NO_WARNINGS)
if(MSVC)
	add_compile_options(/W3 /wd4305 /wd4018 /wd4267 /wd4996 /fp:fast)
	# pointer truncation must never be silent
	add_compile_options(/we4311 /we4312 /we4047 /we4024 /we4133 /we4013)
endif()

set(SHARED_SRC game/q_shared.c)

# ---------------------------------------------------------------- quake2.exe
add_executable(quake2 WIN32
	client/cl_cin.c client/cl_ents.c client/cl_fx.c client/cl_input.c
	client/cl_inv.c client/cl_main.c client/cl_newfx.c client/cl_parse.c
	client/cl_pred.c client/cl_render.c client/cl_scrn.c client/cl_tent.c client/cl_view.c
	client/console.c client/keys.c client/menu.c client/qmenu.c
	client/snd_dma.c client/snd_mem.c client/snd_mix.c client/x86.c
	qcommon/cmd.c qcommon/cmodel.c qcommon/common.c qcommon/crc.c
	qcommon/cvar.c qcommon/files.c qcommon/md4.c qcommon/net_chan.c
	qcommon/pmove.c
	server/sv_ccmds.c server/sv_ents.c server/sv_game.c server/sv_init.c
	server/sv_main.c server/sv_send.c server/sv_user.c server/sv_world.c
	win32/cd_win.c win32/conproc.c win32/in_win.c win32/net_wins.c
	win32/q_shwin.c win32/snd_win.c win32/sys_win.c win32/vid_dll.c
	win32/vid_menu.c win32/q2.rc
	game/m_flash.c ${SHARED_SRC})
target_link_libraries(quake2 PRIVATE winmm wsock32 dxguid dsound dinput8)

# ---------------------------------------------------------------- ref_gl.dll
add_library(ref_gl SHARED
	ref_gl/gl_draw.c ref_gl/gl_image.c ref_gl/gl_light.c ref_gl/gl_mesh.c
	ref_gl/gl_model.c ref_gl/gl_rmain.c ref_gl/gl_rmisc.c ref_gl/gl_rsurf.c
	ref_gl/gl_warp.c
	win32/glw_imp.c win32/qgl_win.c win32/q_shwin.c
	${SHARED_SRC} ref_gl/ref_gl.def)
target_link_libraries(ref_gl PRIVATE winmm opengl32 gdi32 user32)

# -------------------------------------------------------------- ref_soft.dll
add_library(ref_soft SHARED
	ref_soft/r_aclip.c ref_soft/r_alias.c ref_soft/r_bsp.c ref_soft/r_draw.c
	ref_soft/r_edge.c ref_soft/r_image.c ref_soft/r_light.c ref_soft/r_main.c
	ref_soft/r_misc.c ref_soft/r_model.c ref_soft/r_part.c ref_soft/r_poly.c
	ref_soft/r_polyse.c ref_soft/r_rast.c ref_soft/r_scan.c
	ref_soft/r_sprite.c ref_soft/r_surf.c
	win32/rw_ddraw.c win32/rw_dib.c win32/rw_imp.c win32/q_shwin.c
	${SHARED_SRC} ref_soft/ref_soft.def)
target_link_libraries(ref_soft PRIVATE winmm gdi32 user32 ddraw dxguid)

# ------------------------------------------------------------------ game dll
file(GLOB GAME_SRC CONFIGURE_DEPENDS game/*.c)
add_library(game SHARED ${GAME_SRC} game/game.def)
set_target_properties(game PROPERTIES
	OUTPUT_NAME game${Q2_ARCH}
	RUNTIME_OUTPUT_DIRECTORY ${Q2_DATA}/baseq2
	RUNTIME_OUTPUT_DIRECTORY_DEBUG ${Q2_DATA}/baseq2
	RUNTIME_OUTPUT_DIRECTORY_RELEASE ${Q2_DATA}/baseq2
	RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${Q2_DATA}/baseq2
	RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${Q2_DATA}/baseq2)

set_target_properties(quake2 ref_gl ref_soft game PROPERTIES PREFIX "")

# ------------------------------------------------------------ path tracers
# pt/ is the MIT, engine independent core. ref_pt/ is the GPL glue that
# adapts it to the refresh interface, built once per backend.
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REF_PT_SRC
	ref_pt/rpt_main.c ref_pt/rpt_image.c ref_pt/rpt_draw.c ref_pt/rpt_world.c
	ref_pt/rpt_model.c ref_pt/rpt_scene.c ref_pt/rpt_material.c ref_pt/rpt_settings.c
	ref_pt/rpt_water.c ref_pt/rpt_shot.c ref_pt/rpt_offline.c
	pt/water/pt_water.c pt/png/pt_png.c
	win32/q_shwin.c ${SHARED_SRC} ref_pt/ref_pt.def)

add_library(pt_cpu STATIC pt/cpu/pt_cpu.cpp pt/cpu/pt_bvh.cpp pt/cpu/pt_world.cpp pt/cpu/pt_trace.cpp)
# The tracer is all small inlined functions; RelWithDebInfo would otherwise
# hold inlining back. AVX2 is on by default: every x64 CPU since 2013-2015
# has it. Configure with -DPT_AVX2=OFF for something older.
option(PT_AVX2 "Build the CPU path tracer for AVX2" ON)
if(MSVC)
	target_compile_options(pt_cpu PRIVATE /Ob2 /Oi /Ot)
	if(PT_AVX2 AND Q2_ARCH STREQUAL x64)
		target_compile_options(pt_cpu PRIVATE /arch:AVX2)
	endif()
endif()
add_library(ref_ptcpu SHARED ${REF_PT_SRC})
target_link_libraries(ref_ptcpu PRIVATE pt_cpu winmm gdi32 user32)
set_target_properties(ref_ptcpu PROPERTIES PREFIX "")

# the Vulkan backend needs the SDK to build (headers, loader lib, glslc);
# without it everything else still builds and the engine falls back to CPU
find_package(Vulkan COMPONENTS glslc)
if(Vulkan_FOUND AND Vulkan_glslc_FOUND AND Q2_ARCH STREQUAL x64)
	set(PT_SHADER_DIR ${CMAKE_BINARY_DIR}/pt_shaders)
	file(MAKE_DIRECTORY ${PT_SHADER_DIR})
	set(PT_SHADER_INC)
	foreach(shader blit.vert blit.frag trace.comp)
		add_custom_command(
			OUTPUT ${PT_SHADER_DIR}/${shader}.inc
			COMMAND Vulkan::glslc -mfmt=c --target-env=vulkan1.3 -O
				${CMAKE_SOURCE_DIR}/pt/rtx/shaders/${shader} -o ${PT_SHADER_DIR}/${shader}.inc
			DEPENDS ${CMAKE_SOURCE_DIR}/pt/rtx/shaders/${shader}
			VERBATIM)
		list(APPEND PT_SHADER_INC ${PT_SHADER_DIR}/${shader}.inc)
	endforeach()

	add_library(pt_rtx STATIC pt/rtx/pt_rtx.cpp ${PT_SHADER_INC})
	target_include_directories(pt_rtx PRIVATE ${PT_SHADER_DIR})
	target_link_libraries(pt_rtx PUBLIC Vulkan::Vulkan)

	add_library(ref_ptrtx SHARED ${REF_PT_SRC})
	target_compile_definitions(ref_ptrtx PRIVATE RPT_RTX)
	target_link_libraries(ref_ptrtx PRIVATE pt_rtx winmm gdi32 user32)
	set_target_properties(ref_ptrtx PROPERTIES PREFIX "")
else()
	message(STATUS "Vulkan SDK not found or not x64: ref_ptrtx will not be built")
endif()
