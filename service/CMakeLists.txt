# Demon de service morfAnalytics : execute la bibliotheque en tache de fond.
# Le binaire s'appelle « morfanalytics » (comme l'unite systemd et /opt/morfanalytics).
add_executable(morfanalytics main.cpp)

target_link_libraries(morfanalytics PRIVATE morfAnalytics::morfAnalytics Qt6::Core)

if(MSVC)
    target_compile_options(morfanalytics PRIVATE /utf-8)
endif()

install(TARGETS morfanalytics RUNTIME DESTINATION bin)
