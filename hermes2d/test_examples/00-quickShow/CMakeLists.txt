project(00-quickShow)

add_executable(${PROJECT_NAME} main.cpp definitions.cpp definitions.h)

if(NOT MSVC)
  set_property(TARGET ${PROJECT_NAME} PROPERTY COMPILE_FLAGS ${HERMES_FLAGS})
endif()

target_link_libraries(${PROJECT_NAME} ${HERMES2D})
