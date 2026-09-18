set(APP_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

set(APP_SOURCES
    ${APP_ROOT}/app/myMain.c
    ${APP_ROOT}/app/log.c
    ${APP_ROOT}/app/cli.c
    ${APP_ROOT}/app/transport_core.c
    ${APP_ROOT}/third_party/ff16/ff.c
    ${APP_ROOT}/app/fs_utils.c
)
set(APP_INCLUDES 
    ${APP_ROOT}/include
    ${APP_ROOT}/third_party/ff16/
)