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

#include "mod_dup.hh"
#include "Utils.hh"

#include <boost/shared_ptr.hpp>
#include <http_config.h>


namespace DupModule {

/*
 * Callback to iterate over the headers tables
 * Pushes a copy of key => value in a list passed without typing as the first argument
 */
static int iterateOverHeadersCallBack(void *d, const char *key, const char *value) {
    RequestInfo::tHeaders *headers = reinterpret_cast<RequestInfo::tHeaders *>(d);
    headers->push_back(std::pair<std::string, std::string>(key, value));
    return 1;
}

static void
prepareRequestInfo(DupConf *tConf, request_rec *pRequest, RequestInfo &r) {
    // Copy headers in
    apr_table_do(&iterateOverHeadersCallBack, &r.mHeadersIn, pRequest->headers_in, NULL);

    // Basic
    r.mPoison = false;
    r.mConfPath = tConf->dirName;
    r.mPath = pRequest->uri;
    r.mArgs = pRequest->args ? pRequest->args : "";
}

static void
printRequest(request_rec *pRequest, RequestInfo *pBH, DupConf *tConf) {
    const char *reqId = apr_table_get(pRequest->headers_in, c_UNIQUE_ID);
    Log::debug("### Pushing a request with ID: %s, body size:%ld", reqId, pBH->mBody.size());
    Log::debug("### Uri:%s, dir name:%s", pRequest->uri, tConf->dirName);
    Log::debug("### Request args: %s", pRequest->args);
}

/**
 * Output Body filter handler
 * Writes the response body to the RequestInfo
 * Unless not needed because we only duplicate request and no reponses
 */
apr_status_t
outputBodyFilterHandler(ap_filter_t *pFilter, apr_bucket_brigade *pBrigade) {
    request_rec *pRequest = pFilter->r;
    apr_status_t rv;
    // Reject requests that do not meet our requirements
    if ((pFilter->ctx == (void *) -1) || !pRequest || !pRequest->per_dir_config) {
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }
    struct DupConf *tConf = reinterpret_cast<DupConf *>(ap_get_module_config(pRequest->per_dir_config, &dup_module));
    if ((!tConf) || (!tConf->dirName) || (tConf->getHighestDuplicationType() == DuplicationType::NONE)) {
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }

    boost::shared_ptr<RequestInfo> * reqInfo(reinterpret_cast<boost::shared_ptr<RequestInfo> *>(ap_get_module_config(pFilter->r->request_config, &dup_module)));
    if (!reqInfo || !reqInfo->get()) {
        pFilter->ctx = (void *) -1;
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }
    RequestInfo * ri = reqInfo->get();

    // Write the response body to the RequestInfo if found
    apr_bucket *currentBucket;
    for ( currentBucket = APR_BRIGADE_FIRST(pBrigade);
          currentBucket != APR_BRIGADE_SENTINEL(pBrigade);
          currentBucket = APR_BUCKET_NEXT(currentBucket) ) {

        if (APR_BUCKET_IS_EOS(currentBucket)) {
            ri->eos_seen = true;
            pFilter->ctx = (void *) -1;
            rv = ap_pass_brigade(pFilter->next, pBrigade);
            apr_brigade_cleanup(pBrigade);
            return rv;
        }

        if (APR_BUCKET_IS_METADATA(currentBucket))
            continue;

        // We need to get the highest one as we haven't matched which rule it is yet
        if (tConf->getHighestDuplicationType() == DuplicationType::REQUEST_WITH_ANSWER) {

            const char *data;
            apr_size_t len;
            rv = apr_bucket_read(currentBucket, &data, &len, APR_BLOCK_READ);

            if ((rv == APR_SUCCESS) && data) {
                // Appends the part read to the answer
                ri->mAnswer.append(data, len);
            }
        }
    }
    rv = ap_pass_brigade(pFilter->next, pBrigade);
    apr_brigade_cleanup(pBrigade);
    return rv;
}

/**
 * Output filter handler
 * Retrieves in/out headers
 * Pushes the RequestInfo object to the RequestProcessor
 */
apr_status_t
outputHeadersFilterHandler(ap_filter_t *pFilter, apr_bucket_brigade *pBrigade) {
    apr_status_t rv;
    if ( pFilter->ctx == (void *) -1 ) {
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }

    request_rec *pRequest = pFilter->r;
    // Reject requests that do not meet our requirements
    if (!pRequest || !pRequest->per_dir_config) {
        pFilter->ctx = (void *) -1;
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }

    struct DupConf *tConf = reinterpret_cast<DupConf *>(ap_get_module_config(pRequest->per_dir_config, &dup_module));
    if ((!tConf) || (!tConf->dirName) || (tConf->getHighestDuplicationType() == DuplicationType::NONE)) {
        pFilter->ctx = (void *) -1;
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }

    boost::shared_ptr<RequestInfo> * reqInfo(reinterpret_cast<boost::shared_ptr<RequestInfo> *>(ap_get_module_config(pFilter->r->request_config, &dup_module)));

    if (!reqInfo || !reqInfo->get()) {
        pFilter->ctx = (void *) -1;
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }
    RequestInfo *ri = reqInfo->get();

    // Copy headers out
    apr_table_do(&iterateOverHeadersCallBack, &ri->mHeadersOut, pRequest->headers_out, NULL);

    if (!ri->eos_seen) {
        rv = ap_pass_brigade(pFilter->next, pBrigade);
        apr_brigade_cleanup(pBrigade);
        return rv;
    }
    // Pushing the answer to the processor
    prepareRequestInfo(tConf, pRequest, *ri);

    if ( tConf->synchronous ) {
        static __thread CURL * lCurl = NULL;
        if ( ! lCurl ) {
            lCurl = gProcessor->initCurl();
        }
        gProcessor->runOne(*ri, lCurl);
    }
    else {
        gThreadPool->push(*reqInfo);
    }
    pFilter->ctx = (void *) -1;
    printRequest(pRequest, ri, tConf);
    rv = ap_pass_brigade(pFilter->next, pBrigade);
    apr_brigade_cleanup(pBrigade);
    return rv;
}


};
