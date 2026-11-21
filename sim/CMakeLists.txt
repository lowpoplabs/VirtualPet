cmake_minimum_required(VERSION 3.20)
project(virtualpet_sim C)

set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

include(FetchContent)

# SDL2, built from source and linked statically so the exe needs no DLL.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE
)

# LVGL. Keep the major.minor in step with firmware/main/idf_component.yml.
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL2 lvgl)

# LVGL's SDL driver needs the SDL headers.
target_link_libraries(lvgl PUBLIC SDL2::SDL2-static)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../app ${CMAKE_BINARY_DIR}/app)

# Logic checks that need no window: run sim/build/food_test.exe
add_executable(food_test food_test.c)
target_link_libraries(food_test PRIVATE pet_app)

add_executable(pet_core_test pet_core_test.c)
target_link_libraries(pet_core_test PRIVATE pet_app)

add_executable(pet_brain_test pet_brain_test.c)
target_link_libraries(pet_brain_test PRIVATE pet_app)

add_executable(pet_growth_test pet_growth_test.c)
target_link_libraries(pet_growth_test PRIVATE pet_app)

add_executable(pet_days_test pet_days_test.c)
target_link_libraries(pet_days_test PRIVATE pet_app)

add_executable(pet_places_test pet_places_test.c)
target_link_libraries(pet_places_test PRIVATE pet_app)

add_executable(pet_config_test pet_config_test.c)
target_link_libraries(pet_config_test PRIVATE pet_app)

# Replays a foodlog.csv from the watch through the pet core.
add_executable(pet_replay pet_replay.c log_reader.c)
target_link_libraries(pet_replay PRIVATE pet_app)

add_executable(virtualpet_sim main.c log_reader.c screenshot.c)
target_link_libraries(virtualpet_sim PRIVATE pet_app lvgl SDL2::SDL2main SDL2::SDL2-static)

# Artwork check: one picture of the pet in a chosen stage, form, mood and action.
add_executable(look_sheet look_sheet.c screenshot.c)
target_include_directories(look_sheet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app/src)
target_link_libraries(look_sheet PRIVATE pet_app lvgl SDL2::SDL2main SDL2::SDL2-static)
