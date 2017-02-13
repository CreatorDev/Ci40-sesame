# Add executable targets
########################
ADD_EXECUTABLE(sesame_gateway_appd sesame_gateway.c)
# Add library targets
#####################
FIND_LIBRARY(LIB_AWA libawa.so ${STAGING_DIR}/usr/lib)
FIND_LIBRARY(LIB_LETMECREATECORE libletmecreate_core.so ${STAGING_DIR}/usr/lib)
FIND_LIBRARY(LIB_LETMECREATECLICK libletmecreate_click.so ${STAGING_DIR}/usr/lib)
TARGET_LINK_LIBRARIES(sesame_gateway_appd ${LIB_AWA} ${LIB_LETMECREATECORE} ${LIB_LETMECREATECLICK})
	
# Add install targets
######################
INSTALL(TARGETS sesame_gateway_appd RUNTIME DESTINATION bin)
