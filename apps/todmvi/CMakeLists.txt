# ======================================================================
#               todmvi - image -> .dmvi command-line tool
# ======================================================================
set(DMOD_MODULE_NAME        todmvi)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         4096)
set(DMOD_PRIORITY           0)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/todmvi.dmr)
set(DMOD_MAL_IMPLS          )

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    src/todmvi.c
)

target_link_libraries(${DMOD_MODULE_NAME}
    libtodmvi_if
)

# dmview_format.h, which libtodmvi.h includes
dmod_link_modules(${DMOD_MODULE_NAME}
    libdmview@>=0.5
)
