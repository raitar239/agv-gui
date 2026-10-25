qt_add_executable(agv-gui
    main.cpp
    daemonclient.h
    daemonclient.cpp
    iconprovider.h
    iconprovider.cpp
    installedapps.h
    installedapps.cpp
    keyedlistmodel.h
    keyedlistmodel.cpp
    locationmodel.h
    locationmodel.cpp
    systemtheme.h
    systemtheme.cpp
    trayicon.h
    trayicon.cpp
    windowcontroller.h
    windowcontroller.cpp
)

if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(agv-gui
    URI Agv
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/AppIcon.qml
        qml/Banner.qml
        qml/BottomBar.qml
        qml/Card.qml
        qml/ConnectButton.qml
        qml/FlatButton.qml
        qml/InputField.qml
        qml/LocationRow.qml
        qml/NavRail.qml
        qml/Segmented.qml
        qml/Toast.qml
        qml/Toggle.qml
        qml/AppPicker.qml
        qml/HomePage.qml
        qml/LocationsPage.qml
        qml/ExclusionsPage.qml
        qml/SettingsPage.qml
)

target_link_libraries(agv-gui PRIVATE
    agv-common
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
)
target_compile_definitions(agv-gui PRIVATE AGV_VERSION="${PROJECT_VERSION}")
target_compile_options(agv-gui PRIVATE -Wall -Wextra)

install(TARGETS agv-gui DESTINATION ${CMAKE_INSTALL_BINDIR})
