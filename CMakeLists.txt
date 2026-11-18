cmake_minimum_required(VERSION 3.16)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(VSStage LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(VSS_BUILD_UI "Compila o aplicativo com interface (SDL2 + ImGui)" ON)
option(VSS_BUILD_TESTS "Compila os testes" ON)
option(VSS_TEST_RENDERER "Usa o renderizador por software do SDL (apenas para capturas de tela de teste em Linux sem OpenGL)" OFF)
option(VSS_STATIC_RUNTIME "Liga o runtime estaticamente (MinGW)" ON)

# ---------------------------------------------------------------- miniz
add_library(miniz STATIC
  third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
target_include_directories(miniz PUBLIC third_party/miniz)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- núcleo (motor + áudio + biblioteca)
add_library(vsscore STATIC
  engine/MultitrackEngine.cpp engine/PadPlayer.cpp engine/TempoDetect.cpp
  core/Mapper.cpp core/Library.cpp core/Player.cpp core/Audio.cpp core/Decoder.cpp core/MiniaudioImpl.cpp core/Midi.cpp)
target_include_directories(vsscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(vsscore PUBLIC miniz)
# NÃO usar -ffast-math: o stretcher e o limiter dependem de aritmética IEEE previsível.
if(MSVC)
  target_compile_options(vsscore PUBLIC /utf-8)
  target_compile_definitions(vsscore PUBLIC _USE_MATH_DEFINES NOMINMAX)
  target_compile_options(vsscore PRIVATE /O2)
else()
  target_compile_options(vsscore PRIVATE -O2)
endif()

# RtMidi: WinMM no Windows, ALSA no Linux (se existir), senão modo "dummy"
target_sources(vsscore PRIVATE third_party/rtmidi/RtMidi.cpp)
if(WIN32)
  target_compile_definitions(vsscore PUBLIC __WINDOWS_MM__)
  target_link_libraries(vsscore PUBLIC winmm ole32)
else()
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    target_compile_definitions(vsscore PUBLIC __LINUX_ALSA__)
    target_link_libraries(vsscore PUBLIC ${ALSA_LIBRARIES})
    target_include_directories(vsscore PUBLIC ${ALSA_INCLUDE_DIRS})
  else()
    target_compile_definitions(vsscore PUBLIC __RTMIDI_DUMMY__)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(vsscore PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ---------------------------------------------------------------- testes
if(VSS_BUILD_TESTS)
  enable_testing()
  foreach(t mapper_test core_test)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE vsscore)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

# ---------------------------------------------------------------- aplicativo
if(VSS_BUILD_UI)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-2.30.9 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2)

  add_library(imgui STATIC
    third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_sdl2.cpp third_party/imgui/imgui_impl_opengl3.cpp third_party/imgui/imgui_impl_sdlrenderer2.cpp)
  target_include_directories(imgui PUBLIC third_party/imgui)
  target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

  file(GLOB UI_SRC CONFIGURE_DEPENDS src/*.cpp)
  if(WIN32)
    add_executable(VSStage WIN32 ${UI_SRC} src/app.rc)
  else()
    add_executable(VSStage ${UI_SRC})
  endif()
  target_link_libraries(VSStage PRIVATE vsscore imgui SDL2::SDL2main)
  if(VSS_TEST_RENDERER)
    target_compile_definitions(VSStage PRIVATE VSS_TEST_RENDERER=1)
  endif()
  if(WIN32)
    target_link_libraries(VSStage PRIVATE opengl32 comdlg32 shell32 ole32 uuid)
    if(MINGW AND VSS_STATIC_RUNTIME)
      target_link_options(VSStage PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  else()
    # Linux (apenas testes): o ImGui carrega o libGL dinamicamente, não precisa dos headers.
    target_link_libraries(VSStage PRIVATE ${CMAKE_DL_LIBS})
  endif()
endif()
