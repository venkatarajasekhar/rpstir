/**
	Functions used for accessing the RTR database.
*/

#include "logging.h"
#include "rtr.h"


/*==============================================================================
------------------------------------------------------------------------------*/
int getCacheNonce(void *connp, cache_nonce_t *nonce) {
    return getCacheNone((MYSQL*) connp, nonce);
}


/*==============================================================================
------------------------------------------------------------------------------*/
int getLatestSerialNumber(void *connp, serial_number_t *serial) {
    return getLatestSerialNumber((MYSQL*) connp, serial);
}


/*==============================================================================
------------------------------------------------------------------------------*/
int startSerialQuery(void *connp, void ** query_state, serial_number_t serial) {

    return (0);
}


/*==============================================================================
------------------------------------------------------------------------------*/
ssize_t serialQueryGetNext(void *connp, void * query_state, size_t num_rows,
        PDU ** pdus, bool * is_done) {

    return (0);
}


/*==============================================================================
------------------------------------------------------------------------------*/
void stopSerialQuery(void *connp, void * query_state) {

    return;
}


/*==============================================================================
------------------------------------------------------------------------------*/
int startResetQuery(void *connp, void ** query_state) {

    return (0);
}


/*==============================================================================
------------------------------------------------------------------------------*/
ssize_t resetQueryGetNext(void *connp, void * query_state, size_t num_rows,
        PDU ** pdus, bool * is_done) {

    return (0);
}


/*==============================================================================
------------------------------------------------------------------------------*/
void stopResetQuery(void *connp, void * query_state) {

    return;
}
