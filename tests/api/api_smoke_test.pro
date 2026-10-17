QT += core network websockets

CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = api_smoke_test

INCLUDEPATH += ../..

SOURCES += \
    api_smoke_test.cpp \
    ../../src/api/protocol.cpp \
    ../../src/api/api_server.cpp \
    ../../src/api/dto/spot_spec.cpp \
    ../../src/api/dto/state_snapshot.cpp \
    ../../src/api/dto/solve_options.cpp \
    ../../src/api/dto/job_status.cpp \
    ../../src/api/dto/artifact_ref.cpp \
    ../../src/api/events/event_bus.cpp \
    ../../src/api/services/state_store.cpp \
    ../../src/api/services/solver_job_manager.cpp \
    ../../src/api/services/artifact_store.cpp \
    ../../src/api/services/result_query_service.cpp \
    ../../src/api/services/gto_application_service.cpp \
    ../../src/api/services/spot_validator.cpp

HEADERS += \
    ../../src/api/protocol.h \
    ../../src/api/api_server.h \
    ../../src/api/dto/spot_spec.h \
    ../../src/api/dto/state_snapshot.h \
    ../../src/api/dto/solve_options.h \
    ../../src/api/dto/job_status.h \
    ../../src/api/dto/artifact_ref.h \
    ../../src/api/events/event_bus.h \
    ../../src/api/services/state_store.h \
    ../../src/api/services/gto_application_service.h \
    ../../src/api/services/solver_job_runner.h \
    ../../src/api/services/solver_job_manager.h \
    ../../src/api/services/strategy_result_provider.h \
    ../../src/api/services/artifact_store.h \
    ../../src/api/services/result_query_service.h \
    ../../src/api/services/spot_validator.h