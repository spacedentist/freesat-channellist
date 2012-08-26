PROJECT(freesat)

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

INCLUDE_DIRECTORIES(
#    ../include/
   )

ADD_EXECUTABLE(freesat
               freesat.cpp
              )

TARGET_LINK_LIBRARIES(freesat
                      dvbpsi
                     )

INSTALL(TARGETS freesat
        RUNTIME DESTINATION bin)
