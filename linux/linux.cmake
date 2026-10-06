# The Linux build: included from the top CMakeLists.txt in place of the
# Windows targets. Needs SDL2, X11, and for the RTX renderer the Vulkan
# headers and loader and a GLSL compiler (glslc or glslangValidator).

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "The Linux build is 64-bit only")
endif()

set(Q2_OUT ${CMAKE_SOURCE_DIR}/run)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${Q2_OUT})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${Q2_OUT})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(SDL2 REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

# _GNU_SOURCE: q_shlinux.c needs the declaration of mremap, which returns a pointer
add_compile_definitions(_GNU_SOURCE C_ONLY stricmp=strcasecmp strnicmp=strncasecmp _stricmp=strcasecmp)
# The engine is C of 1997: it relies on signed overflow wrapping, on reading
# one type through a pointer to another, and on a char that is signed.
set(Q2_C_FLAGS -fno-strict-aliasing -fwrapv -fsigned-char -fcommon -w)
# every library keeps to its own copy of the functions they all have
set(Q2_LINK_FLAGS -Wl,-Bsymbolic)

set(SHARED_SRC game/q_shared.c)

# -------------------------------------------------------------------- quake2
add_executable(quake2
	client/cl_cin.c client/cl_ents.c client/cl_fx.c client/cl_input.c
	client/cl_inv.c client/cl_main.c client/cl_newfx.c client/cl_parse.c
	client/cl_pred.c client/cl_render.c client/cl_scrn.c client/cl_tent.c client/cl_view.c
	client/console.c client/keys.c client/menu.c client/qmenu.c
	client/snd_dma.c client/snd_mem.c client/snd_mix.c
	qcommon/cmd.c qcommon/cmodel.c qcommon/common.c qcommon/crc.c
	qcommon/cvar.c qcommon/files.c qcommon/md4.c qcommon/net_chan.c
	qcommon/pmove.c
	server/sv_ccmds.c server/sv_ents.c server/sv_game.c server/sv_init.c
	server/sv_main.c server/sv_send.c server/sv_user.c server/sv_world.c
	linux/sys_sdl.c linux/vid_sdl.c linux/snd_sdl.c linux/net_udp.c
	linux/q_shlinux.c linux/glob.c null/cd_null.c win32/vid_menu.c
	game/m_flash.c ${SHARED_SRC})
target_compile_options(quake2 PRIVATE ${Q2_C_FLAGS})
target_include_directories(quake2 PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(quake2 PRIVATE ${SDL2_LIBRARIES} m dl)

# ---------------------------------------------------------------------- game
file(GLOB GAME_SRC CONFIGURE_DEPENDS game/*.c)
add_library(game SHARED ${GAME_SRC})
target_compile_options(game PRIVATE ${Q2_C_FLAGS})
target_link_options(game PRIVATE ${Q2_LINK_FLAGS})
target_link_libraries(game PRIVATE m)
set_target_properties(game PROPERTIES PREFIX "" OUTPUT_NAME gamex64
	LIBRARY_OUTPUT_DIRECTORY ${Q2_OUT}/baseq2)

# -------------------------------------------------------------------- ref_gl
# The original OpenGL renderer, in a window and context made by SDL.
find_package(OpenGL REQUIRED)
add_library(ref_gl SHARED
	ref_gl/gl_draw.c ref_gl/gl_image.c ref_gl/gl_light.c ref_gl/gl_mesh.c
	ref_gl/gl_model.c ref_gl/gl_rmain.c ref_gl/gl_rmisc.c ref_gl/gl_rsurf.c
	ref_gl/gl_warp.c
	linux/gl_sdl.c linux/qgl_linux.c linux/q_shlinux.c linux/glob.c
	${SHARED_SRC})
target_compile_options(ref_gl PRIVATE ${Q2_C_FLAGS})
target_include_directories(ref_gl PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_options(ref_gl PRIVATE ${Q2_LINK_FLAGS})
target_link_libraries(ref_gl PRIVATE OpenGL::GL ${SDL2_LIBRARIES} m)
set_target_properties(ref_gl PROPERTIES PREFIX "")

# -------------------------------------------------------------- path tracers
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REF_PT_SRC
	ref_pt/rpt_main.c ref_pt/rpt_image.c ref_pt/rpt_draw.c ref_pt/rpt_world.c
	ref_pt/rpt_model.c ref_pt/rpt_scene.c ref_pt/rpt_material.c ref_pt/rpt_settings.c
	ref_pt/rpt_water.c ref_pt/rpt_shot.c ref_pt/rpt_offline.c ref_pt/rpt_bench.c ref_pt/rpt_sdl.c
	pt/water/pt_water.c pt/png/pt_png.c
	linux/q_shlinux.c linux/glob.c ${SHARED_SRC})

option(PT_AVX2 "Build the CPU path tracer for AVX2" ON)
set(PT_CXX_FLAGS -O2 -ffast-math -fno-finite-math-only)
if(PT_AVX2)
	list(APPEND PT_CXX_FLAGS -mavx2 -mfma)
endif()

add_library(pt_cpu STATIC pt/cpu/pt_cpu.cpp pt/cpu/pt_bvh.cpp pt/cpu/pt_world.cpp pt/cpu/pt_trace.cpp)
target_compile_options(pt_cpu PRIVATE ${PT_CXX_FLAGS})
target_include_directories(pt_cpu PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(pt_cpu PUBLIC ${X11_LIBRARIES} Threads::Threads)

function(q2_ref name backend)
	add_library(${name} SHARED ${REF_PT_SRC})
	target_compile_options(${name} PRIVATE ${Q2_C_FLAGS})
	target_include_directories(${name} PRIVATE ${SDL2_INCLUDE_DIRS})
	target_link_options(${name} PRIVATE ${Q2_LINK_FLAGS})
	target_link_libraries(${name} PRIVATE ${backend} ${SDL2_LIBRARIES} m)
	set_target_properties(${name} PROPERTIES PREFIX "")
endfunction()

q2_ref(ref_ptcpu pt_cpu)

# The RTX renderer. Its shaders are compiled to C arrays of SPIR-V: by glslc
# where there is one, as on Windows, or else by glslangValidator, whose
# output is the same numbers without the braces around them.
find_package(Vulkan)
find_program(PT_GLSLC glslc)
find_program(PT_GLSLANG glslangValidator)
if(Vulkan_FOUND AND (PT_GLSLC OR PT_GLSLANG))
	set(PT_SHADER_DIR ${CMAKE_BINARY_DIR}/pt_shaders)
	file(MAKE_DIRECTORY ${PT_SHADER_DIR})
	set(PT_SHADER_INC)
	foreach(shader blit.vert blit.frag trace.comp temporal.comp atrous.comp compose.comp
			bloom.comp grade.comp resolve.comp)
		set(src ${CMAKE_SOURCE_DIR}/pt/rtx/shaders/${shader})
		set(out ${PT_SHADER_DIR}/${shader}.inc)
		if(PT_GLSLC)
			add_custom_command(OUTPUT ${out}
				COMMAND ${PT_GLSLC} -mfmt=c --target-env=vulkan1.3 -O ${src} -o ${out}
				DEPENDS ${src} ${CMAKE_SOURCE_DIR}/pt/rtx/shaders/scene.glsl
				VERBATIM)
		else()
			add_custom_command(OUTPUT ${out}
				COMMAND ${PT_GLSLANG} -V --target-env vulkan1.3 -x -o ${out}.hex ${src}
				COMMAND ${CMAKE_COMMAND} -DIN=${out}.hex -DOUT=${out} -P ${CMAKE_SOURCE_DIR}/linux/spv_inc.cmake
				DEPENDS ${src} ${CMAKE_SOURCE_DIR}/pt/rtx/shaders/scene.glsl ${CMAKE_SOURCE_DIR}/linux/spv_inc.cmake
				VERBATIM)
		endif()
		list(APPEND PT_SHADER_INC ${out})
	endforeach()

	add_library(pt_rtx STATIC pt/rtx/pt_rtx.cpp pt/cpu/pt_world.cpp pt/cpu/pt_bvh.cpp ${PT_SHADER_INC})
	target_compile_options(pt_rtx PRIVATE ${PT_CXX_FLAGS})
	target_include_directories(pt_rtx PRIVATE ${PT_SHADER_DIR} ${X11_INCLUDE_DIR})
	target_link_libraries(pt_rtx PUBLIC Vulkan::Vulkan ${X11_LIBRARIES} Threads::Threads)

	q2_ref(ref_ptrtx pt_rtx)
	target_compile_definitions(ref_ptrtx PRIVATE RPT_RTX)
else()
	message(STATUS "Vulkan or a GLSL compiler not found: ref_ptrtx will not be built")
endif()
