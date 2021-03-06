/*
* mod_dup - duplicates apache requests
*
* Copyright (C) 2013 Orange
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once

//#include <apr_pools.h>
//#include <apr_hooks.h>

#include <httpd.h>

namespace MigrateModule {

    extern const unsigned int CMaxBytes;
    extern const char* c_UNIQUE_ID;

    /*
     * Returns the next random request ID
     * method is reentrant
     */
    unsigned int getNextReqId();

    bool extractBrigadeContent(apr_bucket_brigade *bb, ap_filter_t *pF, std::string &content);

};

namespace DupModule {

    extern const unsigned int CMaxBytes;
    extern const char* c_UNIQUE_ID;

    /*
     * Returns the next random request ID
     * method is reentrant
     */
    unsigned int getNextReqId();

    bool extractBrigadeContent(apr_bucket_brigade *bb, ap_filter_t *pF, std::string &content);

};
