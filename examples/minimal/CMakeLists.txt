find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(morfanalytics_demo main.cpp)
target_link_libraries(morfanalytics_demo PRIVATE morfAnalytics::morfAnalytics Qt6::Core)

if(MSVC)
    target_compile_options(morfanalytics_demo PRIVATE /utf-8)
endif()
