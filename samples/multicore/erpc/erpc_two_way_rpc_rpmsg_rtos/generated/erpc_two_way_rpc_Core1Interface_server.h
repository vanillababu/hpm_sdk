/*
 * Copyright 2018 NXP
 * Copyright (c) 2022 HPMicro
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * Generated by erpcgen 1.9.1 on Thu Nov 24 13:53:27 2022.
 *
 * AUTOGENERATED - DO NOT EDIT
 */


#if !defined(_erpc_two_way_rpc_Core1Interface_server_h_)
#define _erpc_two_way_rpc_Core1Interface_server_h_

#ifdef __cplusplus
#include "erpc_server.hpp"
#include "erpc_codec.hpp"
extern "C"
{
#include "erpc_two_way_rpc_Core1Interface.h"
#include <stdint.h>
#include <stdbool.h>
}

#if 10901 != ERPC_VERSION_NUMBER
#error "The generated shim code version is different to the rest of eRPC code."
#endif


/*!
 * @brief Service subclass for Core1Interface.
 */
class Core1Interface_service : public erpc::Service
{
public:
    Core1Interface_service() : Service(kCore1Interface_service_id) {}

    /*! @brief Call the correct server shim based on method unique ID. */
    virtual erpc_status_t handleInvocation(uint32_t methodId, uint32_t sequence, erpc::Codec * codec, erpc::MessageBufferFactory *messageFactory);

private:
    /*! @brief Server shim for increaseNumber of Core1Interface interface. */
    erpc_status_t increaseNumber_shim(erpc::Codec * codec, erpc::MessageBufferFactory *messageFactory, uint32_t sequence);

    /*! @brief Server shim for getGetCallbackFunction of Core1Interface interface. */
    erpc_status_t getGetCallbackFunction_shim(erpc::Codec * codec, erpc::MessageBufferFactory *messageFactory, uint32_t sequence);

    /*! @brief Server shim for getNumberFromCore0 of Core1Interface interface. */
    erpc_status_t getNumberFromCore0_shim(erpc::Codec * codec, erpc::MessageBufferFactory *messageFactory, uint32_t sequence);
};

extern "C" {
#else
#include "erpc_two_way_rpc_Core1Interface.h"
#endif // __cplusplus

typedef void * erpc_service_t;

erpc_service_t create_Core1Interface_service(void);

#if ERPC_ALLOCATION_POLICY == ERPC_ALLOCATION_POLICY_DYNAMIC
void destroy_Core1Interface_service(erpc_service_t service);
#elif ERPC_ALLOCATION_POLICY == ERPC_ALLOCATION_POLICY_STATIC
void destroy_Core1Interface_service(void);
#else
#warning "Unknown eRPC allocation policy!"
#endif

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _erpc_two_way_rpc_Core1Interface_server_h_
