set(TARGET FFRRandomizer)

add_library(${TARGET} SHARED
    dllmain.cpp
    Battles.cpp
    Debug.cpp
    Encounters.cpp
    Helpers.cpp
    Items.cpp
    Panel.cpp
    Save.cpp
    ScalingModel.cpp
    ShrineSwap.cpp
)

target_include_directories(${TARGET} PRIVATE .)
target_link_libraries(${TARGET} PUBLIC UE4SS)
