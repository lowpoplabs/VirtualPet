# Shared app code. Builds two ways:
#  - as an ESP-IDF component (firmware/ adds this folder to EXTRA_COMPONENT_DIRS)
#  - as a plain static library for the desktop simulator (sim/)

set(PET_APP_SRCS
    src/pet_app.c
    src/pet_badges.c
    src/pet_brain.c
    src/pet_core.c
    src/pet_power.c
    src/pet_save.c
    src/pet_sound.c
    src/pet_config.c
    src/pet_food.c
    src/pet_growth.c
    src/pet_journal.c
    src/pet_pages.c
    src/pet_motion.c
    src/pet_places.c
    src/pet_view.c
)

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${PET_APP_SRCS}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES lvgl
    )
else()
    add_library(pet_app STATIC ${PET_APP_SRCS})
    target_include_directories(pet_app PUBLIC include PRIVATE src)
    target_link_libraries(pet_app PUBLIC lvgl)
endif()
