/*
 * License: 5G-MAG Public License (v1.0)
 * Author: Dev Audsin
 * Copyright: (C) 2022 British Broadcasting Corporation
 *
 * For full license terms please see the LICENSE file distributed with this
 * program. If this file is missing then the license can be retrieved from
 * https://drive.google.com/file/d/1cinCiA778IErENZ3JN52VFW-1ffHpx7Z/view
 */


#include "ogs-sbi.h"
#include "sbi-path.h"
#include "context.h"
#include "certmgr.h"
#include "server.h"
#include "response-cache-control.h"

void msaf_state_initial(ogs_fsm_t *s, msaf_event_t *e)
{
    msaf_sm_debug(e);

    ogs_assert(s);

    OGS_FSM_TRAN(s, &msaf_state_functional);
}

void msaf_state_final(ogs_fsm_t *s, msaf_event_t *e)
{
    msaf_sm_debug(e);

    ogs_assert(s);
}

void msaf_state_functional(ogs_fsm_t *s, msaf_event_t *e)
{
    int rv;

    ogs_sbi_stream_t *stream = NULL;
    ogs_sbi_request_t *request = NULL;

    ogs_sbi_nf_instance_t *nf_instance = NULL;
    ogs_sbi_subscription_data_t *subscription_data = NULL;
    ogs_sbi_response_t *response = NULL;
    ogs_sbi_message_t message;
    ogs_sbi_xact_t *sbi_xact = NULL;

    msaf_sm_debug(e);

    ogs_assert(s);

    switch (e->h.id) {
        case OGS_FSM_ENTRY_SIG:
            ogs_info("[%s] MSAF Running", ogs_sbi_self()->nf_instance->id);
	    msaf_context_server_name_set();
            break;

        case OGS_FSM_EXIT_SIG:
            break;

        case OGS_EVENT_SBI_SERVER:
            request = e->h.sbi.request;
            ogs_assert(request);
            stream = e->h.sbi.data;
            ogs_assert(stream);

            rv = ogs_sbi_parse_header(&message, &request->h);
            if (rv != OGS_OK) {
                ogs_error("ogs_sbi_parse_header() failed");              
                ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_BAD_REQUEST, 1, NULL, "cannot parse HTTP message", NULL, NULL));    
                
                break;
            }

            SWITCH(message.h.service.name)
                CASE(OGS_SBI_SERVICE_NAME_NNRF_NFM)
                    if (strcmp(message.h.api.version, OGS_SBI_API_V1) != 0) {
                        ogs_error("Not supported version [%s]", message.h.api.version);
                        ogs_assert(true == ogs_sbi_server_send_error(
                                    stream, OGS_SBI_HTTP_STATUS_BAD_REQUEST,
                                    &message, "Not supported version", NULL));
                        ogs_sbi_message_free(&message);
                        break;
                    }
                    SWITCH(message.h.resource.component[0])
                        CASE(OGS_SBI_RESOURCE_NAME_NF_STATUS_NOTIFY)
                            SWITCH(message.h.method)
                                CASE(OGS_SBI_HTTP_METHOD_POST)
                                    ogs_nnrf_nfm_handle_nf_status_notify(stream, &message);
                                    break;

                                DEFAULT
                                    ogs_error("Invalid HTTP method [%s]", message.h.method);
                                    ogs_assert(true ==
                                        ogs_sbi_server_send_error(stream,
                                            OGS_SBI_HTTP_STATUS_FORBIDDEN, &message,
                                            "Invalid HTTP method", message.h.method));
                            END
                            break;

                        DEFAULT
                            ogs_error("Invalid resource name [%s]",
                                    message.h.resource.component[0]);
                            ogs_assert(true ==
                                    ogs_sbi_server_send_error(stream,
                                        OGS_SBI_HTTP_STATUS_BAD_REQUEST, &message,
                                        "Invalid resource name",
                                        message.h.resource.component[0]));
                    END
                    ogs_sbi_message_free(&message);
                    break;

                CASE("3gpp-m1")
                    if (strcmp(message.h.api.version, "v2") != 0) {
                        char *error;
                        ogs_error("Not supported version [%s]", message.h.api.version);

                        error = ogs_msprintf("Version [%s] not supported", message.h.api.version);

                        ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_BAD_REQUEST, 1, NULL, "Not supported version", error, NULL));    
                                    
                        ogs_sbi_message_free(&message);
                        ogs_free(error);
                        break;
                    }
                    SWITCH(message.h.resource.component[0])

                        CASE("provisioning-sessions")
                            SWITCH(message.h.method)
                                CASE(OGS_SBI_HTTP_METHOD_POST)

                                    if (message.h.resource.component[1] && message.h.resource.component[2] && message.h.resource.component[3]) {
                                        msaf_provisioning_session_t *msaf_provisioning_session;
                                        
                                        if (!strcmp(message.h.resource.component[2],"content-hosting-configuration") && !strcmp(message.h.resource.component[3],"purge")) {
                                            {
                                                ogs_hash_index_t *hi;
                                                for (hi = ogs_hash_first(request->http.headers);
                                                    hi; hi = ogs_hash_next(hi)) {
                                                    if (!ogs_strcasecmp(ogs_hash_this_key(hi), OGS_SBI_CONTENT_TYPE)) {
                                                        if (ogs_strcasecmp(ogs_hash_this_val(hi), "application/x-www-form-urlencoded")) {
                                                            char *err = NULL;
                                                            char *type = NULL;
                                                            type = (char *)ogs_hash_this_val(hi);
                                                            ogs_error("Unsupported Media Type: received type: %s, should have been application/x-www-form-urlencoded", type);
                                                            asprintf(&err, "Unsupported Media Type: received type: %s, should have been application/x-www-form-urlencoded", type);
                                        
                                                            ogs_assert(true == nf_server_send_error(stream, 415, 3, &message, "Unsupported Media Type.", err, NULL));   
                                                            ogs_sbi_message_free(&message);                                                            
                                                            return; 
                                                            
                                                        }
                                                    }
                                                }
                                            }
                                            msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                            if(msaf_provisioning_session) {
                                                // process the POST body
                                                purge_resource_id_node_t *purge_cache;
                                                msaf_application_server_state_node_t *as_state;
                                                assigned_provisioning_sessions_node_t *assigned_provisioning_sessions_resource;                                                 
                                                const char *component = ogs_msprintf("%s/content-hosting-configuration/purge", message.h.resource.component[1]);
						                        m1_purge_information_t *m1_purge_info = ogs_calloc(1, sizeof(m1_purge_information_t));
					                            m1_purge_info->m1_stream = stream;
                                                m1_purge_info->m1_message = message;

                                                ogs_list_for_each(&msaf_provisioning_session->msaf_application_server_state_nodes, as_state) { 
                                                    if(as_state->application_server && as_state->application_server->canonicalHostname) {
                                                        ogs_list_for_each(&as_state->assigned_provisioning_sessions,assigned_provisioning_sessions_resource){
                                                            if(!strcmp(assigned_provisioning_sessions_resource->assigned_provisioning_session->provisioningSessionId, msaf_provisioning_session->provisioningSessionId)) {

                                                                purge_cache = ogs_calloc(1, sizeof(purge_resource_id_node_t));
                                                                ogs_assert(purge_cache);
                                                                purge_cache->provisioning_session_id = ogs_strdup(assigned_provisioning_sessions_resource->assigned_provisioning_session->provisioningSessionId);
								                
								                                purge_cache->m1_purge_info = m1_purge_info;
								                                m1_purge_info->refs++;
								                                if(request->http.content)
                                                                    purge_cache->purge_regex = ogs_strdup(request->http.content);
								                                else
                                                                    purge_cache->purge_regex = NULL;

                                                                if (ogs_list_first(&as_state->purge_content_hosting_cache) == NULL)
                                                                    ogs_list_init(&as_state->purge_content_hosting_cache);

                                                                ogs_list_add(&as_state->purge_content_hosting_cache, purge_cache);
                                                            } else {
								                                ogs_error("Provisioning Session [%s] is not assigned to an Application Server", message.h.resource.component[1]);
						                                        char *err = NULL;
                                                                asprintf(&err,"Provisioning Session [%s] is not assigned to an Application Server", message.h.resource.component[1]);
                                                                ogs_assert(true == nf_server_send_error(stream, 500, 3, &message, "Provisioning session is not assigned to an Application Server.", err, NULL));  

							                                }
                                                        }
                                                    } else {
							                            ogs_error("Provisioning Session [%s]: Unable to get information about Application Server", message.h.resource.component[1]);
						                                char *err = NULL;
                                                        asprintf(&err,"Provisioning Session [%s] : Unable to get information about Application Server", message.h.resource.component[1]);
                                                        ogs_assert(true == nf_server_send_error(stream, 500, 3, &message, "Unable to get information about Application Server", err, NULL)); 
						                            }

                                                    next_action_for_application_server(as_state);
                                                    ogs_free(component);

                                                }
						                        if (m1_purge_info->refs == 0) {
							                        ogs_free(m1_purge_info);
							                        // Send 204 back to M1 client
						                        }
                                            } else {
                                                ogs_error("Unable to retrieve the Provisioning Session [%s]", message.h.resource.component[1]);
						                        char *err = NULL;
                                                asprintf(&err,"Provisioning session [%s] does not exist.", message.h.resource.component[1]);
                                                ogs_error("Provisioning session [%s] does not exist.", message.h.resource.component[1]);                                               
                                                ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Provisioning session does not exist.", err, NULL));    

                                            }

                                        }
                                    
                                    } else if (message.h.resource.component[1] && message.h.resource.component[2]) {
                                        msaf_provisioning_session_t *msaf_provisioning_session;
                                        if (!strcmp(message.h.resource.component[2],"content-hosting-configuration")) {
                                            msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                            if(msaf_provisioning_session) {
                                                // process the POST body
						                        cJSON *entry;
                                                int rv;
                                                cJSON *chc;
                                                cJSON *content_hosting_config = cJSON_Parse(request->http.content);
						                        char *txt = cJSON_Print(content_hosting_config);
						                        ogs_debug("txt:%s", txt);

                                                cJSON_ArrayForEach(entry, content_hosting_config) {
                                                    if(!strcmp(entry->string, "entryPointPath")){
							                            if(!uri_relative_check(entry->valuestring)) {
                                                            char *err = NULL;
                                                            asprintf(&err,"While creating the Content Hosting Configuration for the Provisioning Session [%s], entryPointPath does not match the regular expression [%s].",message.h.resource.component[1], entry->valuestring );
                                                            ogs_error("While creating the Content Hosting Configuration for the Provisioning Session [%s], entryPointPath does not match the regular expression [%s].",message.h.resource.component[1], entry->valuestring);
                                    
                                                            ogs_assert(true == nf_server_send_error(stream, 422, 2, &message, "Entry Point Path does not match the regular expression.", err, NULL));    
    
                                                            cJSON_Delete(content_hosting_config);
								                            break;
                                                    }

                                                    }
						                        }

                                                if(msaf_provisioning_session->contentHostingConfiguration) {
                                                    OpenAPI_content_hosting_configuration_free(msaf_provisioning_session->contentHostingConfiguration);
                                                    msaf_provisioning_session->contentHostingConfiguration = NULL;

                                                }

                                                if (msaf_provisioning_session->serviceAccessInformation) {
                                                    OpenAPI_service_access_information_resource_free(msaf_provisioning_session->serviceAccessInformation);
                                                    msaf_provisioning_session->serviceAccessInformation = NULL;
                                                }
                                            
                                               
                                                rv = msaf_distribution_create(content_hosting_config, msaf_provisioning_session);
                                                
                                                if(rv){
                                                    
                                                    ogs_debug("Content Hosting Configuration created successfully");
                                                    msaf_application_server_state_set_on_post(msaf_provisioning_session);
                                                    chc = msaf_get_content_hosting_configuration_by_provisioning_session_id(message.h.resource.component[1]);
                                                    if (chc != NULL) {
                                                        char *text;
							                            msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                                        response = nf_server_new_response(request->h.uri, "application/json",  msaf_provisioning_session->contentHostingConfigurationReceived, msaf_provisioning_session->contentHostingConfigurationHash, msaf_self()->config.server_response_cache_control->m1_content_hosting_configurations_response_max_age, "m1 contentHostingConfiguration");
                                                        text = cJSON_Print(chc);
                                                        nf_server_populate_response(response, strlen(text), text, 201);
                                                        ogs_assert(response);
                                                        ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                                        cJSON_Delete(chc);
                                                        cJSON_Delete(content_hosting_config);
                                                    } else {
                                                    char *err = NULL;                                                    
                                                    ogs_error("Unable to retrieve the Content Hosting Configuration for the Provisioning Session [%s].", message.h.resource.component[1]);
                                                    asprintf(&err,"Unable to retrieve the Content Hosting Configuration for the Provisioning Session [%s].", message.h.resource.component[1]);
                                                    ogs_assert(true == nf_server_send_error(stream, 404, 2, &message, "Unable to retrieve the Content Hosting Configuration.", err, NULL));    

                                                    }
                                                    
 
                                                } else {
                                                    char *err = NULL;
                                                    
                                                    ogs_error("Failed to populate Content Hosting Configuration for the Provisioning Session [%s].", message.h.resource.component[1]);
                                                    asprintf(&err,"Creation of the Content Hosting Configuration failed for the Provisioning Session [%s]", message.h.resource.component[1]);
                                                    ogs_assert(true == nf_server_send_error(stream, 500, 2, &message, "Creation of the Content Hosting Configuration failed.", err, NULL));     
    
                                                }

                                            } else {
                                                char *err = NULL;
                                                asprintf(&err,"Provisioning session [%s]does not exist.", message.h.resource.component[1]);
                                                ogs_error("Provisioning session [%s] does not exist.", message.h.resource.component[1]);
                                               
                                                ogs_assert(true == nf_server_send_error(stream, 404, 2, &message, "Provisioning session does not exist.", err, NULL));    
        

                                            }

                                        }
                                        if (!strcmp(message.h.resource.component[2],"certificates")) {
                                            ogs_info("POST certificates");
                			                ogs_hash_index_t *hi;
                                            char *canonical_domain_name;
					                        char *cert;
					                        int csr = 0;

					                        {
                				                for (hi = ogs_hash_first(request->http.params);
                        				            hi; hi = ogs_hash_next(hi)) {
                    					            if (!ogs_strcasecmp(ogs_hash_this_key(hi), "csr")) {
								                        csr = 1;
								                        break;
                   					                }
              				                    }
            				                }
					    
                                            msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
					                        if (msaf_provisioning_session) {
					                            if (csr) {

				                                    msaf_certificate_t *csr_cert;
						                            char *location; 
					                                csr_cert = server_cert_new("newcsr", NULL);
						                            ogs_sbi_response_t *response;                                                   
                                                    location = ogs_msprintf("%s/%s", request->h.uri, csr_cert->id);
						                            response = nf_server_new_response(location, "application/x-pem-file",  msaf_provisioning_session->contentHostingConfigurationReceived, msaf_provisioning_session->contentHostingConfigurationHash, msaf_self()->config.server_response_cache_control->m1_content_hosting_configurations_response_max_age, "m1 contentHostingConfiguration");

                                                    response->http.content_length = strlen(csr_cert->certificate);
                                                    response->http.content = ogs_strdup(csr_cert->certificate);
                                                    response->status = 200;                                                  
                                                    ogs_assert(response);
                                                    ogs_assert(true == ogs_sbi_server_send_response(stream, response));
						                            ogs_free(location);
						                            ogs_free(csr_cert->certificate);
						                            ogs_free(csr_cert);
						   
					                                return;
					                            }
                                                if (ogs_list_first(&msaf_provisioning_session->msaf_application_servers) == NULL) {
                                                    msaf_application_server_node_t *msaf_as = NULL;
                                                    msaf_as = ogs_list_first(&msaf_self()->config.applicationServers_list);
                                                    canonical_domain_name = msaf_as->canonicalHostname;
                                                    ogs_list_init(&msaf_provisioning_session->msaf_application_server_state_nodes);
                                                    ogs_list_add(&msaf_provisioning_session->msaf_application_server_state_nodes, msaf_as);
                                                }
					                            ogs_info("canonical_domain_name): %s", canonical_domain_name);
                                                cert = check_in_cert_list(canonical_domain_name);
					                            if (cert != NULL) {
                                                    ogs_sbi_response_t *response;
						                            char *location;
                                                    response = ogs_sbi_response_new();
                                                    response->status = 200;
						                            location = ogs_msprintf("%s/%s", request->h.uri, cert);
                                                    ogs_sbi_header_set(response->http.headers, "Location", location);
                                                    ogs_assert(response);
                                                    ogs_assert(true == ogs_sbi_server_send_response(stream, response));
					                                ogs_free(location);
                                                } else {
				                                    msaf_certificate_t *new_cert;
						                            new_cert = server_cert_new("newcert", NULL);
						                            ogs_sbi_response_t *response;
                                                    char *location;
                                                    response = ogs_sbi_response_new();
                                                    response->status = 200;
                                                    location = ogs_msprintf("%s/%s", request->h.uri, new_cert->id);
                                                    ogs_sbi_header_set(response->http.headers, "Location", location);
                                                    ogs_assert(response);
                                                    ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                                    ogs_free(location);
						                            ogs_free(new_cert);


                                                }

                                            } else {
                                                char *err = NULL;
                                                asprintf(&err,"Provisioning session [%s] does not exists.", message.h.resource.component[1]);
                                                ogs_error("Provisioning session [%s] does not exists.", message.h.resource.component[1]);
                                               
                                                ogs_assert(true == nf_server_send_error(stream, 404, 2, &message, "Provisioning session does not exists.", err, NULL));    
        

                                            }

                                        }

                                    } else {
                                        cJSON *entry;
                                        cJSON *prov_sess = cJSON_Parse(request->http.content);
                                        cJSON *provisioning_session;
                                        char *provisioning_session_type, *external_app_id, *asp_id = NULL;
                                        msaf_provisioning_session_t *msaf_provisioning_session;

                                        cJSON_ArrayForEach(entry, prov_sess) {
                                            if(!strcmp(entry->string, "provisioningSessionType")){
                                                provisioning_session_type = entry->valuestring;
                                            }
                                            if(!strcmp(entry->string, "aspId")){
                                                asp_id = entry->valuestring;
                                            }
                                            if(!strcmp(entry->string, "externalApplicationId")){
                                                external_app_id = entry->valuestring;
                                            }

                                        }
                                        msaf_provisioning_session = msaf_provisioning_session_create(provisioning_session_type, asp_id, external_app_id);
                                        provisioning_session = msaf_provisioning_session_get_json(msaf_provisioning_session->provisioningSessionId);
                                        if (provisioning_session != NULL) {
                                            ogs_sbi_response_t *response;
                                            char *text;
                                            char *location;
                                            text = cJSON_Print(provisioning_session);                                
                                            if (request->h.uri[strlen(request->h.uri)-1] != '/') {
                                                location = ogs_msprintf("%s/%s", request->h.uri,msaf_provisioning_session->provisioningSessionId);
                                            } else {
                                                location = ogs_msprintf("%s%s", request->h.uri,msaf_provisioning_session->provisioningSessionId);
                                            }
                                            response = nf_server_new_response(location, "application/json",  msaf_provisioning_session->provisioningSessionReceived, msaf_provisioning_session->provisioningSessionHash, msaf_self()->config.server_response_cache_control->m1_provisioning_session_response_max_age, "m1 provisioningSession");
        
                                            nf_server_populate_response(response, strlen(text), text, 201);
                                            ogs_assert(response);
                                            ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                            ogs_free(location);
                                            cJSON_Delete(provisioning_session);
                                            cJSON_Delete(prov_sess);
                                        } else {
                                            char *err = NULL;
                                            asprintf(&err,"Creation of the Provisioning session failed.");
                                            ogs_error("Creation of the Provisioning session failed.");
                                            ogs_assert(true == nf_server_send_error(stream, 404, 1, &message, "Creation of the Provisioning session failed.", err, NULL));    
            
                                        }
                                    }

                                    break;

                                CASE(OGS_SBI_HTTP_METHOD_GET)
				                    if (message.h.resource.component[1] && message.h.resource.component[2] && message.h.resource.component[3]) {
                                        if (!strcmp(message.h.resource.component[2],"certificates") ) {
					                        msaf_provisioning_session_t *msaf_provisioning_session;
                                            msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                            if (msaf_provisioning_session) {
                                                msaf_certificate_t *cert;
                                                ogs_sbi_response_t *response;

                                                cert = server_cert_retrieve(message.h.resource.component[3]);

                                                if(!cert) {
                                                    ogs_error("unable to retrieve certificate [%s]", message.h.resource.component[3]);
                                                    return;
                                                }

                                                response = ogs_sbi_response_new();
                                                if(!cert->return_code) {
                                                response->http.content_length = strlen(cert->certificate);
                                                response->http.content = ogs_strdup(cert->certificate);
                                                response->status = 200;
                                                ogs_sbi_header_set(response->http.headers, "Content-Type", "application/x-pem-file");
                                                ogs_assert(response);
                                                ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                                } else if(cert->return_code == 4){
                                                    char *err = NULL;
                                                    asprintf(&err,"Certificate [%s] does not exists.", cert->id);
                                                    ogs_error("Certificate [%s] does not exists.", cert->id);
                                                    ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Certificate does not exists.", err, NULL));    

                                                } else if(cert->return_code == 8){
                                                    char *err = NULL;
                                                    asprintf(&err,"Certificate [%s] not yet available.", cert->id);
                                                    ogs_error("Certificate [%s] not yet available.", cert->id);
                                                    ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Certificate not yet available.", err, NULL));    

                                                            

                                                } else {
                                                    char *err = NULL;
                                                    asprintf(&err,"Certificate [%s] management problem.", cert->id);
                                                    ogs_error("Certificate [%s] management problem.", cert->id);                                                   
                                                    ogs_assert(true == nf_server_send_error(stream, 500, 3, &message, "Certificate management problem.", err, NULL));    


                                                }
                                                ogs_free(cert->certificate);
                                                ogs_free(cert);

                                            } else {
                                                char *err = NULL;
                                                asprintf(&err,"Provisioning session [%s] is not available.", message.h.resource.component[1]);
                                                ogs_error("Provisioning session [%s] is not available.", message.h.resource.component[1]);                                    
                                                ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Provisioning session does not exists.", err, NULL));    
      

                                            }
                                        }
                                    }
                                    else
                                    if (message.h.resource.component[1] && message.h.resource.component[2]) {
                                        msaf_provisioning_session_t *msaf_provisioning_session;
                                        if (!strcmp(message.h.resource.component[2],"content-hosting-configuration")) {
                                            msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                            if(msaf_provisioning_session) {
                                                    cJSON *chc;
                                                    chc = msaf_get_content_hosting_configuration_by_provisioning_session_id(message.h.resource.component[1]);
                                                    if (chc != NULL) {
                                                        ogs_sbi_response_t *response;
                                                        char *text;
                                                        text = cJSON_Print(chc);
                                                     
                                                        response = nf_server_new_response(request->h.uri, "application/json",  msaf_provisioning_session->contentHostingConfigurationReceived, msaf_provisioning_session->contentHostingConfigurationHash, msaf_self()->config.server_response_cache_control->m1_content_hosting_configurations_response_max_age, "m1 contentHostingConfiguration");
                                                        ogs_assert(response);
                                                        nf_server_populate_response(response, strlen(text), text, 200);
                                                        ogs_assert(true == ogs_sbi_server_send_response(stream, response));

                                                        cJSON_Delete(chc);
                                                    } else {
                                                    char *err = NULL;
					                                ogs_error("Provisioning Session [%s]: Unable to retrieve the Content Hosting Configuration", message.h.resource.component[1]);
                                                    asprintf(&err,"Provisioning Session [%s]: Unable to retrieve the Content Hosting Configuration", message.h.resource.component[1]);
                                                                                                        
                                                    ogs_assert(true == nf_server_send_error(stream, 404, 2, &message, "Unable to retrieve the Content Hosting Configuration.", err, NULL));    



                                                    }

                                            } else {
                                                char *err = NULL;
                                                asprintf(&err,"Provisioning Session [%s] does not exist.", message.h.resource.component[1]);
                                                ogs_error("Provisioning Session [%s] does not exist.", message.h.resource.component[1]);
                                                   
                                                ogs_assert(true == nf_server_send_error(stream, 404, 2, &message, "Provisioning session does not exist.", err, NULL));       

                                            }

                                        }

                                    } else if (message.h.resource.component[1]) {
                                        msaf_provisioning_session_t *msaf_provisioning_session = NULL;
                                        cJSON *provisioning_session = NULL;

                                        msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);

                                        provisioning_session = msaf_provisioning_session_get_json(message.h.resource.component[1]);

                                        if (provisioning_session && msaf_provisioning_session && !msaf_provisioning_session->marked_for_deletion) {
                                            ogs_sbi_response_t *response;
                                            char *text;
                                            text = cJSON_Print(provisioning_session);
                                            
                                            response = nf_server_new_response(NULL, "application/json",  msaf_provisioning_session->provisioningSessionReceived, msaf_provisioning_session->provisioningSessionHash, msaf_self()->config.server_response_cache_control->m1_provisioning_session_response_max_age, "m1 provisioningSession");
        
                                            nf_server_populate_response(response, strlen(text), text, 200);
                                            ogs_assert(response);
                                            ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                            cJSON_Delete(provisioning_session);

                                        } else {
                                            char *err = NULL;
                                            asprintf(&err,"Provisioning Session [%s] is not available.", message.h.resource.component[1]);
                                            ogs_error("Provisioning Session [%s] is not available.", message.h.resource.component[1]);
                                                 
                                            ogs_assert(true == nf_server_send_error(stream, 404, 2, &message, "Provisioning session does not exists.", err, NULL));    
                                        }
                                    }
                                    break;
				  				   
                                CASE(OGS_SBI_HTTP_METHOD_PUT)
                                    if (message.h.resource.component[1] && message.h.resource.component[2]) {
					    
					                    ogs_info("PUT: %s", message.h.resource.component[1]);	
                                        msaf_provisioning_session_t *msaf_provisioning_session;
                                        msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                        if(msaf_provisioning_session) {
					                        ogs_info("PUT: with msaf_provisioning_session: %s", message.h.resource.component[1]);	
                                            if (!strcmp(message.h.resource.component[2],"content-hosting-configuration")) {

                                                // process the POST body
						                        cJSON *entry;
                                                int rv;
                                                cJSON *content_hosting_config = cJSON_Parse(request->http.content);
						                        char *txt = cJSON_Print(content_hosting_config);
						                        ogs_debug("txt:%s", txt);

                                                cJSON_ArrayForEach(entry, content_hosting_config) {
                                                    if(!strcmp(entry->string, "entryPointPath")){
							                            if(!uri_relative_check(entry->valuestring)) {
                                                           char *err = NULL;
                                                            asprintf(&err,"While updating the Content Hosting Configuration for the Provisioning Session [%s], Entry Point Path does not match the regular expression [%s].",message.h.resource.component[1], entry->valuestring );
                                                            ogs_error("While updating the Content Hosting Configuration for the provisioning Session [%s], Entry Point Path does not match the regular expression [%s].",message.h.resource.component[1], entry->valuestring);
                                    
                                                            ogs_assert(true == nf_server_send_error(stream, 422, 2, &message, "Entry Point Path does not match the regular expression.", err, NULL));    
    
                                                            cJSON_Delete(content_hosting_config);
								                            break;
                                                    }

                                                    }
						                        }
                                                if(msaf_provisioning_session->contentHostingConfiguration) {
                                                    OpenAPI_content_hosting_configuration_free(msaf_provisioning_session->contentHostingConfiguration);
                                                    msaf_provisioning_session->contentHostingConfiguration = NULL;

                                                }

                                                if (msaf_provisioning_session->serviceAccessInformation) {
                                                    OpenAPI_service_access_information_resource_free(msaf_provisioning_session->serviceAccessInformation);
                                                    msaf_provisioning_session->serviceAccessInformation = NULL;
                                                }


                                                rv = msaf_distribution_create(content_hosting_config, msaf_provisioning_session);

                                                if(rv){

                                                    msaf_application_server_state_update(msaf_provisioning_session);

                                                    ogs_debug("Content Hosting Configuration updated successfully");

                                                    ogs_sbi_response_t *response;
                                                    response = ogs_sbi_response_new();
                                                    response->status = 204;
                                                    ogs_sbi_header_set(response->http.headers, "Content-Type", "application/json");
                                                    ogs_sbi_header_set(response->http.headers, "Location", request->h.uri);
                                                    ogs_assert(response);
                                                    ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                                    cJSON_Delete(content_hosting_config);

                                                } else {
                                                    char *err = NULL;
                                                    ogs_error("Provisioning Session [%s]: Failed to update the Content Hosting Configuration.", message.h.resource.component[1]);
                                                    asprintf(&err,"Provisioning Session [%s]: Update to Content Hosting Configuration failed.", message.h.resource.component[1]);
                                                    ogs_error("Update of the Content Hosting Configuration failed.");
                                                     
                                                    ogs_assert(true == nf_server_send_error(stream, 404, 2, &message, "Failed to update the contentHostingConfiguration.", err, NULL));

                                                }
                                            }
                                            if (message.h.resource.component[1] && !strcmp(message.h.resource.component[2],"certificates") && message.h.resource.component[3]) {
                                                char *cert_id;
                                                char *cert;
                                                int rv;
						                        ogs_sbi_response_t *response;
						                        msaf_provisioning_session_t *msaf_provisioning_session;

                                                {
                                                    ogs_hash_index_t *hi;
                                                    for (hi = ogs_hash_first(request->http.headers);
                                                        hi; hi = ogs_hash_next(hi)) {
                                                        if (!ogs_strcasecmp(ogs_hash_this_key(hi), OGS_SBI_CONTENT_TYPE)) {
                                                            if (ogs_strcasecmp(ogs_hash_this_val(hi), "application/x-pem-file")) {
								                                char *err = NULL;
								                                char *type = NULL;
								                                type = (char *)ogs_hash_this_val(hi);
								                                ogs_error("Unsupported Media Type: received type: %s, should have been application/x-pem-file", type);
                                                                asprintf(&err, "Unsupported Media Type: received type: %s, should have been application/x-pem-file", type);
                                                                
                                                                ogs_assert(true == nf_server_send_error(stream, 415, 3, &message, "Unsupported Media Type.", err, NULL));
                                                                ogs_sbi_message_free(&message);
                                                                return; 
                                                                
                                                            }
                                                        }
                                                    }
                                                }

                                                msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);

                                                if(msaf_provisioning_session) {
                                                    cert_id = message.h.resource.component[3];
                                                    cert = ogs_strdup(request->http.content);
                                                    rv = server_cert_set(cert_id, cert);
				         	                        // response = ogs_sbi_response_new();

                                                    if (rv == 0){
 
 						                                response = ogs_sbi_response_new();
                                                        response->status = 204;
                                                        ogs_assert(response);
                                                        ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                                    } else if (rv == 3 ) {

                                                        char *err = NULL;
                                                        ogs_error("A server certificate with id [%s] already exist", cert_id);
                                                        asprintf(&err,"A server certificate with id [%s] already exist", cert_id);                                                       
                                                        ogs_assert(true == nf_server_send_error(stream, 403, 3, &message, "A server certificate already exist.", err, NULL));    

                                                    } else if(rv == 4) {
                                                        char *err = NULL;
                                                        ogs_error("Server certificate with id [%s] does not exist", cert_id);
                                                        asprintf(&err,"Server certificate with id [%s] does not exist", cert_id);                                                        
                                                        ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Server certificate does not exist.", err, NULL));    
    
                                                    } else if(rv == 5) {
                                                        char *err = NULL;
                                                        ogs_error("CSR was never generated for this certificate Id [%s]", cert_id);
                                                        asprintf(&err,"CSR was never generated for this certificate Id [%s]", cert_id);                                                        
                                                        ogs_assert(true == nf_server_send_error(stream, 400, 3, &message, "CSR was never generated for the certificate.", err, NULL));    


                                                    } else if(rv == 6) {
                                                        char *err = NULL;
                                                        ogs_error("The public certificate [%s] provided does not match the key", cert_id);
                                                        asprintf(&err,"The public certificate [%s] provided does not match the key", cert_id);                                                        
                                                        ogs_assert(true == nf_server_send_error(stream, 400, 3, &message, "The public certificate provided does not match the key.", err, NULL));    
    
                                                    } else {
                                                        char *err = NULL;
                                                        ogs_error("There was a certificate management problem for the certificate id [%s]", cert_id);
                                                        asprintf(&err,"There was a certificate management problem for the certificate id [%s].", cert_id);
                                                        
                                                        ogs_assert(true == nf_server_send_error(stream, 500, 3, &message, "There was a certificate management problem.", err, NULL));    
  

                                                    }
				                                    ogs_free(cert);
						                        }

                                            }

                                        } else {
                                            char *err = NULL;
                                            asprintf(&err,"Provisioning Session [%s] does not exist.", message.h.resource.component[1]);
                                            ogs_error("Provisioning Session [%s] does not exist.", message.h.resource.component[1]);                                        
                                            ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Provisioning session does not exist.", err, NULL));     

                                        }
					
                                    
                                    }
                                    break;
    
                                CASE(OGS_SBI_HTTP_METHOD_DELETE)
			        		
			                        if (message.h.resource.component[1] && !strcmp(message.h.resource.component[2],"certificates") && message.h.resource.component[3]) { 
                                        ogs_sbi_response_t *response;
                                        msaf_provisioning_session_t *provisioning_session = NULL;
                                        provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                        if (provisioning_session) {
                                            int rv;
                                            rv = server_cert_delete(message.h.resource.component[3]);
					                        if ((rv == 0) || (rv == 8)){

                                                response = ogs_sbi_response_new();
                                                response->status = 204;
                                                ogs_assert(response);
                                                ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                            } else if (rv == 4 ) {
                                                char *err = NULL;
                                                asprintf(&err,"Certificate [%s] does not exist.", message.h.resource.component[3]);
                                                ogs_error("Certificate [%s] does not exist.", message.h.resource.component[3]);
                                               
                                                ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Certificate does not exist.", err, NULL));     

                                            } else {
                                            char *err = NULL;
                                            asprintf(&err,"Certificate management problem for certificate [%s].", message.h.resource.component[3]);
                                            ogs_error("Certificate management problem.");
                                           
                                            ogs_assert(true == nf_server_send_error(stream, 500, 3, &message, "Certificate management problem.", err, NULL));     
    

                                            }

                                        } else {
                                            char *err = NULL;
                                            asprintf(&err,"Provisioning Session [%s] does not exist.", message.h.resource.component[1]);
                                            ogs_error("Provisioning session  [%s] does not exist.", message.h.resource.component[1]);
                                            
                                            ogs_assert(true == nf_server_send_error(stream, 404, 3, &message, "Provisioning session does not exist.", err, NULL));     
        

                                        }
                                    } else 

                                   if (message.h.resource.component[1]) {
                                        ogs_sbi_response_t *response;
                                        msaf_provisioning_session_t *provisioning_session = NULL;
                                        provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);
                                        if(!provisioning_session || provisioning_session->marked_for_deletion){
                                            char *err = NULL;
                                            asprintf(&err,"Provisioning session [%s] either not found or already marked for deletion.", message.h.resource.component[1]);

                                            ogs_error("Provisioning session [%s] either not found or already marked for deletion.",message.h.resource.component[1]);
                                                    
                                            ogs_assert(true == nf_server_send_error(stream, 500, 3, &message, "Provisioning session either not found or already marked for deletion..", err, NULL));     
            
                                        } else {
                                            provisioning_session->marked_for_deletion = 1;

                                            response = ogs_sbi_response_new();
                                            ogs_assert(response);
                                            response->status = 202;
                                            ogs_assert(true == ogs_sbi_server_send_response(stream, response));

                                            msaf_delete_content_hosting_configuration(message.h.resource.component[1]);
                                            msaf_delete_certificate(message.h.resource.component[1]);
                                            msaf_context_provisioning_session_free(provisioning_session);
                                            msaf_provisioning_session_hash_remove(message.h.resource.component[1]);

                                        }
                                    }

                                    break;

                                DEFAULT
                                    ogs_error("Invalid HTTP method [%s]", message.h.method);                                          
                                    ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_FORBIDDEN, 0, &message, "Invalid HTTP method", message.h.method, NULL));     
        
                            END
                            break;

                        DEFAULT
                            char *err;
                            ogs_error("Invalid resource name [%s]",
                                    message.h.resource.component[0]);
                            asprintf(&err,"Invalid resource name [%s]", message.h.resource.component[0]);                            
                            ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_BAD_REQUEST, 0, &message, "Invalid resource name", err, NULL));     
           
                    END
                    ogs_sbi_message_free(&message);
                    break;

                CASE("3gpp-m5")
                    if (strcmp(message.h.api.version, "v2") != 0) {
                        char *error;
                        ogs_error("Not supported version [%s]", message.h.api.version);
                        
                        error = ogs_msprintf("Version [%s] not supported", message.h.api.version);

                        ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_BAD_REQUEST, 1, NULL, "Not supported version", error, NULL));    
                                    
                        ogs_sbi_message_free(&message);
                        ogs_free(error);    
                        
                        break;
                    }
                    SWITCH(message.h.resource.component[0])
                        CASE("service-access-information")
                            SWITCH(message.h.method)
                                CASE(OGS_SBI_HTTP_METHOD_GET)
                                    cJSON *service_access_information;
		                            msaf_provisioning_session_t *msaf_provisioning_session = NULL;
                                    msaf_provisioning_session = msaf_provisioning_session_find_by_provisioningSessionId(message.h.resource.component[1]);

				                    if(msaf_provisioning_session == NULL) {
				                        char *err = NULL;
                                        asprintf(&err,"Provisioning Session [%s] not found.", message.h.resource.component[1]);
                                        ogs_error("Client requested invalid Provisioning Session [%s]", message.h.resource.component[1]);                                       
                                        ogs_assert(true == nf_server_send_error(stream, 404, 1, &message, "Provisioning Session not found.", err, NULL));     
                
				                    } else if (msaf_provisioning_session->serviceAccessInformation) {
                                        service_access_information = msaf_context_retrieve_service_access_information(message.h.resource.component[1]);
                                        if (service_access_information != NULL) {
                                            ogs_sbi_response_t *response;
                                            char *text;
                                            text = cJSON_Print(service_access_information);
                                            response = nf_server_new_response(NULL, "application/json",  msaf_provisioning_session->serviceAccessInformationCreated, msaf_provisioning_session->serviceAccessInformationHash, msaf_self()->config.server_response_cache_control->m5_service_access_information_response_max_age, "m5");
                                            nf_server_populate_response(response, strlen(text), text, 201);                                         
                                            ogs_assert(response);
                                            ogs_assert(true == ogs_sbi_server_send_response(stream, response));
                                            cJSON_Delete(service_access_information);
                                        } else {
                                            char *err = NULL;
                                            asprintf(&err,"Service Access Information for the Provisioning Session [%s] not found.", message.h.resource.component[1]);
                                            ogs_error("Client requested invalid Service Access Information for the Provisioning Session [%s]", message.h.resource.component[1]);
                                            
                                            ogs_assert(true == nf_server_send_error(stream, 404, 1, &message, "Service Access Information not found.", err, NULL));     

                                                        
                                        }
				    } else {
                                            char *err = NULL;
                                            asprintf(&err,"Provisioning Session [%s] has no Service Access Information associated with it.", message.h.resource.component[1]);
                                            ogs_error("Provisioning Session [%s] has no Service Access Information associated with it", message.h.resource.component[1]);
                                          
                                            ogs_assert(true == nf_server_send_error(stream, 404, 1, &message, "Service Access Information not found.", err, NULL));     
            
                                    }
                                    break;
                                DEFAULT
                                    ogs_error("Invalid HTTP method [%s]", message.h.method);
                                           
                                    ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_FORBIDDEN, 1, &message, "Invalid HTTP method.", message.h.method, NULL));     
        
                            END
                            break;
                        DEFAULT
                            ogs_error("Invalid resource name [%s]",
                                    message.h.resource.component[0]);
                            
                            ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_BAD_REQUEST, 1, &message, "Invalid resource name.", message.h.resource.component[0], NULL));     
            
                    END
                    ogs_sbi_message_free(&message);
                    break;
                DEFAULT
                    ogs_error("Invalid API name [%s]", message.h.service.name);
                   
                    ogs_assert(true == nf_server_send_error(stream, OGS_SBI_HTTP_STATUS_BAD_REQUEST, 1, &message, "Invalid API name.",  message.h.service.name, NULL));     

            END
            break;

        case OGS_EVENT_SBI_CLIENT:
            ogs_assert(e);

            response = e->h.sbi.response;
            ogs_assert(response);
            rv = ogs_sbi_parse_header(&message, &response->h);
            if (rv != OGS_OK) {
                ogs_error("ogs_sbi_parse_header() failed");
                ogs_sbi_message_free(&message);
                ogs_sbi_response_free(response);
                break;
            }
            {
                ogs_hash_index_t *hi;
                for (hi = ogs_hash_first(response->http.headers);
                        hi; hi = ogs_hash_next(hi)) {
                    if (!ogs_strcasecmp(ogs_hash_this_key(hi), OGS_SBI_CONTENT_TYPE)) {
                        message.http.content_type = ogs_hash_this_val(hi);
                    } else if (!ogs_strcasecmp(ogs_hash_this_key(hi), OGS_SBI_LOCATION)) {
                        message.http.location = ogs_hash_this_val(hi);
                    }
                }
            }

            message.res_status = response->status;

            SWITCH(message.h.service.name)
                CASE("3gpp-m3")
                    SWITCH(message.h.resource.component[0])
                        CASE("content-hosting-configurations")

                            msaf_application_server_state_node_t *as_state;
                            as_state = e->application_server_state;
                            ogs_assert(as_state);

                            if (message.h.resource.component[1] && message.h.resource.component[2]) {
                                
                                if (!strcmp(message.h.resource.component[2],"purge")) {

                                    SWITCH(message.h.method)
                                        CASE(OGS_SBI_HTTP_METHOD_POST)
					                        purge_resource_id_node_t *purge_node = e->purge_node;
                                            
                                            if (response->status == 204 || response->status == 200) {
						
						                        purge_resource_id_node_t *content_hosting_cache, *next = NULL;
					       				
                                                if (response->status == 200) {
    					   	                        //parse the int in response body
    					   	                        //Add the integer to purge_node->m1_purge_info->purged_entries_total;
					                                {
                                                        ogs_hash_index_t *hi;
                                                        for (hi = ogs_hash_first(request->http.headers);
                                                            hi; hi = ogs_hash_next(hi)) {
                                                            if (!ogs_strcasecmp(ogs_hash_this_key(hi), OGS_SBI_CONTENT_TYPE)) {
                                                                if (ogs_strcasecmp(ogs_hash_this_val(hi), "application/json")) {
                                                                    char *err = NULL;
                                                                    char *type = NULL;
                                                                    type = (char *)ogs_hash_this_val(hi);
                                                                    ogs_error("Unsupported Media Type: received type: %s, should have been application/x-www-form-urlencoded", type);
                                                                    asprintf(&err, "Unsupported Media Type: received type: %s, should have been application/x-www-form-urlencoded", type);
                                                                   
                                                                    ogs_assert(true == nf_server_send_error(stream, 415, 2, &message, "Provisioning session does not exist.", err, NULL));
                                                                    ogs_sbi_message_free(&message);
                                                                    return;
                                                            
                                                            
                                                                }
                                                            }
                                                        }
                                                    }

				                                    int purged_items_from_as =  0;
						                            cJSON *entry;
    					                            cJSON *number_of_cache_entries = cJSON_Parse(response->http.content);
    						                        cJSON_ArrayForEach(entry, number_of_cache_entries) {
						                            ogs_debug("Purged entries return %d\n", entry->valueint);
						                            purged_items_from_as = entry->valueint;

    							                    }
						                            purge_node->m1_purge_info->purged_entries_total += purged_items_from_as;

    					   	                    }
								    

                                                ogs_list_for_each_safe(&as_state->purge_content_hosting_cache, next, content_hosting_cache){
                                                    if(purge_node->purge_regex)
						                            {
                                    
                                                        if(!strcmp(content_hosting_cache->provisioning_session_id, purge_node->provisioning_session_id) && !strcmp(content_hosting_cache->purge_regex, purge_node->purge_regex))
                                                                break;
                                                    } else if(!strcmp(content_hosting_cache->provisioning_session_id, purge_node->provisioning_session_id))
                                                    {        
                                                        break;
                                                    }
                                                }
						                        if(content_hosting_cache){
							                        ogs_list_remove(&as_state->purge_content_hosting_cache, content_hosting_cache);
							                        ogs_debug("M1 List Purge refs: %d, Event Purge node refs: %d ", content_hosting_cache->m1_purge_info->refs, purge_node->m1_purge_info->refs); 
							                
                                                    purge_node->m1_purge_info->refs--;
							                        ogs_debug(" After decrement, M1 List Purge refs: %d, Event Purge node refs: %d ", content_hosting_cache->m1_purge_info->refs, purge_node->m1_purge_info->refs);
                                                    if(!purge_node->m1_purge_info->refs){    
      								                    //  send M1 response with total from purge_node->m1_purge_info->purged_entries_total
                                                        //  ogs_free(purge_node->m1_purge_info);
							                        ogs_sbi_response_t *response;							                       
							                        cJSON *purged_entries_total_json = cJSON_CreateNumber(purge_node->m1_purge_info->purged_entries_total);
							                        char *purged_entries_total = cJSON_Print(purged_entries_total_json);                                                   
						                            response = ogs_sbi_response_new();                                            		
							                        response->http.content_length = strlen(purged_entries_total);
                                            		response->http.content = purged_entries_total;
                                                    response->status = 200;							
							                        ogs_sbi_header_set(response->http.headers, "Content-Type", "application/json");
                                                    ogs_assert(response);
                                                    ogs_assert(true == ogs_sbi_server_send_response(purge_node->m1_purge_info->m1_stream, response));
                                                    
                                                    if(content_hosting_cache->m1_purge_info) ogs_free(content_hosting_cache->m1_purge_info);
                                                    if (content_hosting_cache->provisioning_session_id) ogs_free(content_hosting_cache->provisioning_session_id);
                                                    if(content_hosting_cache->purge_regex) ogs_free(content_hosting_cache->purge_regex);
                                                    ogs_free(content_hosting_cache);                                               
							                        } 
                                                    msaf_application_server_state_log(&as_state->purge_content_hosting_cache, "Purge Content Hosting Cache list");
                                                    
						                        }
					                        }
                                            
                                            
				                            if((response->status == 404) || (response->status == 413) || (response->status == 414) || (response->status == 415) || (response->status == 422) || (response->status == 500) || (response->status == 503)) 
                                            {
                                                char *error;
                                                purge_resource_id_node_t *content_hosting_cache, *next = NULL;
						                        cJSON *purge_cache_err = NULL;
                                                if(response->http.content){
						                            purge_cache_err = cJSON_Parse(response->http.content);
                                                    char *txt = cJSON_Print(purge_cache_err);
                                                    ogs_debug("txt:%s", txt);
						                        }
                                                   
                                                if(response->status == 404) {

						                            ogs_error("Error message from the Application Server [%s] with response code [%d]: Cache not found\n", as_state->application_server->canonicalHostname, response->status);
						                        } else if(response->status == 413) {
						                            ogs_error("Error message from the Application Server [%s] with response code [%d]: Pay load too large\n", as_state->application_server->canonicalHostname, response->status);
                                                } else if(response->status == 414) {
						                            ogs_error("Error message from the Application Server [%s] with response code [%d]: URI too long\n", as_state->application_server->canonicalHostname, response->status);
                                                } else if(response->status == 415) {
						                            ogs_error("Error message from the Application Server [%s] with response code [%d]: Unsupported media type\n", as_state->application_server->canonicalHostname, response->status);
                                                } else if(response->status == 422) {
						                            ogs_error("Error message from the Application Server [%s] with response code [%d]: Unprocessable Entity\n", as_state->application_server->canonicalHostname, response->status);
						                        } else if(response->status == 500) {
						                            ogs_error("Error message from the Application Server [%s] with response code [%d]: Internal server error\n", as_state->application_server->canonicalHostname, response->status);
						                        } else if(response->status == 503) {
						                            ogs_error("Error message from the Application Server [%s] with response code [%d]: Service Unavailable\n", as_state->application_server->canonicalHostname, response->status);
						                        } else {
                                                    
					                                ogs_error("Application Server [%s] sent unrecognised response code [%d]", as_state->application_server->canonicalHostname, response->status);
						                        } 	

                                                if (purge_node->purge_regex) {
                                                        error = ogs_msprintf("Application Server possibly encountered problem with regex %s", purge_node->purge_regex);
                                                    } else {
                                                        error = ogs_msprintf("Application Server unable to process the contained instructions"); 
                                                    }
                                                		
                                                
						                        ogs_assert(true == nf_server_send_error( purge_node->m1_purge_info->m1_stream,
                                                        response->status, 3, &purge_node->m1_purge_info->m1_message, "Problem occured during cache purge", error, purge_cache_err));

						                        ogs_list_for_each_safe(&as_state->purge_content_hosting_cache, next, content_hosting_cache){
                                                    if (purge_node->purge_regex) {
                                                        if(!strcmp(content_hosting_cache->provisioning_session_id, purge_node->provisioning_session_id) && !strcmp(content_hosting_cache->purge_regex, purge_node->purge_regex)) {
                                                        
                                                            ogs_list_remove(&as_state->purge_content_hosting_cache, content_hosting_cache);
                                                            ogs_debug("M1 List Purge refs: %d, Event Purge node refs: %d ", content_hosting_cache->m1_purge_info->refs, purge_node->m1_purge_info->refs); 
                                                            if(content_hosting_cache->m1_purge_info) ogs_free(content_hosting_cache->m1_purge_info);
                                                            if (content_hosting_cache->provisioning_session_id) ogs_free(content_hosting_cache->provisioning_session_id);
                                                            if(content_hosting_cache->purge_regex) ogs_free(content_hosting_cache->purge_regex);
                                                            ogs_free(content_hosting_cache);
                                                                                  
                                                        } 
                                                    } else if(!strcmp(content_hosting_cache->provisioning_session_id, purge_node->provisioning_session_id)) {
                                                        
                                                            ogs_list_remove(&as_state->purge_content_hosting_cache, content_hosting_cache);
                                                            ogs_debug("M1 List Purge refs: %d, Event Purge node refs: %d ", content_hosting_cache->m1_purge_info->refs, purge_node->m1_purge_info->refs); 
                                                            if(content_hosting_cache->m1_purge_info) ogs_free(content_hosting_cache->m1_purge_info);
                                                            if (content_hosting_cache->provisioning_session_id) ogs_free(content_hosting_cache->provisioning_session_id);
                                                            if(content_hosting_cache->purge_regex) ogs_free(content_hosting_cache->purge_regex);
                                                            ogs_free(content_hosting_cache);
                                                                                  
                                                    } 
                                                }  
                        						ogs_free(error);
							                    cJSON_Delete(purge_cache_err);

                                            } 
                                                                                      
                                            next_action_for_application_server(as_state);	 
                                            break;
                                    END
                                    break;
                            
                                } 
                            } else  if (message.h.resource.component[1]) {
        
                                SWITCH(message.h.method)
                                    CASE(OGS_SBI_HTTP_METHOD_POST)

                                        if (response->status == 201) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] recieved for Content Hosting Configuration [%s]", message.h.resource.component[0], message.h.method, response->status, message.h.resource.component[1]);

                                            resource_id_node_t *content_hosting_configuration;
                                            ogs_list_for_each(&as_state->upload_content_hosting_configurations,content_hosting_configuration) {
                                                if(!strcmp(content_hosting_configuration->state, message.h.resource.component[1]))
                                                    break;
                                            }
                                            if(content_hosting_configuration) {

                                                ogs_debug("Removing %s from upload_content_hosting_configurations", content_hosting_configuration->state);
                                                ogs_list_remove(&as_state->upload_content_hosting_configurations, content_hosting_configuration);
                                                ogs_debug("Adding %s to current_content_hosting_configurations",content_hosting_configuration->state);
                                                ogs_list_add(as_state->current_content_hosting_configurations, content_hosting_configuration);
                                            }

                                        }
                                        if(response->status == 405){
                                            ogs_error("Content Hosting Configuration resource already exist at the specified path\n");
                                        }
                                        if(response->status == 413){
                                            ogs_error("Payload too large\n");
                                        }
                                        if(response->status == 414){
                                            ogs_error("URI too long\n");
                                        }
                                        if(response->status == 415){
                                            ogs_error("Unsupported media type\n");
                                        }
                                        if(response->status == 500){
                                            ogs_error("Internal server error\n");
                                        }
                                        if(response->status == 503){
                                            ogs_error("Service unavailable\n");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    CASE(OGS_SBI_HTTP_METHOD_PUT)
                                        if(response->status == 200 || response->status == 204) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] recieved for Content Hosting Configuration [%s]", message.h.resource.component[0], message.h.method, response->status, message.h.resource.component[1]);
                                            resource_id_node_t *content_hosting_configuration;
                                            ogs_list_for_each(&as_state->upload_content_hosting_configurations,content_hosting_configuration){
                                                if(!strcmp(content_hosting_configuration->state, message.h.resource.component[1]))
                                                    break;
                                            }
                                            if(content_hosting_configuration) {

                                                ogs_debug("Removing %s from upload_content_hosting_configurations", content_hosting_configuration->state);
                                                ogs_free(content_hosting_configuration->state);
                                                ogs_list_remove(&as_state->upload_content_hosting_configurations, content_hosting_configuration);
                                                ogs_free(content_hosting_configuration);
                                            }

                                        }
                                        if(response->status == 404){
                                            ogs_error("Not Found\n");
                                        }
                                        if(response->status == 413){
                                            ogs_error("Payload too large\n");
                                        }
                                        if(response->status == 414){
                                            ogs_error("URI too long\n");
                                        }
                                        if(response->status == 415){
                                            ogs_error("Unsupported Media Type\n");
                                        }
                                        if(response->status == 500){
                                            ogs_error("Internal Server Error\n");
                                        }
                                        if(response->status == 503){
                                            ogs_error("Service Unavailable\n");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    CASE(OGS_SBI_HTTP_METHOD_DELETE)
                                        if(response->status == 204) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] recieved for Content Hosting Configuration [%s]", message.h.resource.component[0], message.h.method, response->status,message.h.resource.component[1]);

                                            resource_id_node_t *content_hosting_configuration, *next = NULL;
                                            resource_id_node_t *delete_content_hosting_configuration, *node = NULL;

                                            if(as_state->current_content_hosting_configurations) {

                                                ogs_list_for_each_safe(as_state->current_content_hosting_configurations, next, content_hosting_configuration){

                                                    if(!strcmp(content_hosting_configuration->state, message.h.resource.component[1]))
                                                        break;
                                                }
                                            }

                                            if(content_hosting_configuration) {

                                                msaf_application_server_state_log(as_state->current_content_hosting_configurations, "Current Content Hosting Configurations");

                                                ogs_debug("Removing %s from current_content_hosting_configurations", content_hosting_configuration->state);
                                                ogs_free(content_hosting_configuration->state);
                                                ogs_list_remove(as_state->current_content_hosting_configurations, content_hosting_configuration);
                                                ogs_free(content_hosting_configuration);
                                                msaf_application_server_state_log(as_state->current_content_hosting_configurations, "Current Content Hosting Configurations");
                                            }

                                            ogs_list_for_each_safe(&as_state->delete_content_hosting_configurations, node, delete_content_hosting_configuration) {

                                                if (!strcmp(delete_content_hosting_configuration->state, message.h.resource.component[1])) {

                                                    msaf_application_server_state_log(&as_state->delete_content_hosting_configurations, "Delete Content Hosting Configurations");

                                                    ogs_debug("Destroying Content Hosting Configuration: %s", delete_content_hosting_configuration->state);
                                                    ogs_free(delete_content_hosting_configuration->state);
                                                    ogs_list_remove(&as_state->delete_content_hosting_configurations, delete_content_hosting_configuration);
                                                    ogs_free(delete_content_hosting_configuration);

                                                    msaf_application_server_state_log(&as_state->delete_content_hosting_configurations, "Delete Content Hosting Configurations");
                                                }
                                            }

                                        }
                                        if(response->status == 404){
                                            ogs_error("Not Found\n");
                                        }
                                        if(response->status == 413){
                                            ogs_error("Payload too large\n");
                                        }
                                        if(response->status == 414){
                                            ogs_error("URI too long\n");
                                        }
                                        if(response->status == 415){
                                            ogs_error("Unsupported Media Type\n");
                                        }
                                        if(response->status == 500){
                                            ogs_error("Internal Server Error\n");
                                        }
                                        if(response->status == 503){
                                            ogs_error("Service Unavailable\n");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    DEFAULT
                                        ogs_error("Unknown M3 Content Hosting Configuration operation [%s]", message.h.resource.component[1]);
                                        break;
                                END
                                break;
                            } else {
                                cJSON *entry;
                                cJSON *chc_array = cJSON_Parse(response->http.content);
                                resource_id_node_t *current_chc;
                                SWITCH(message.h.method)
                                    CASE(OGS_SBI_HTTP_METHOD_GET)

                                        if(response->status == 200) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] for Content Hosting Configuration operation [%s]",
                                                    message.h.resource.component[0], message.h.method, response->status, message.h.resource.component[1]);

                                            if (as_state->current_content_hosting_configurations == NULL) {
                                                as_state->current_content_hosting_configurations = ogs_calloc(1,sizeof(*as_state->current_content_hosting_configurations));
                                                ogs_assert(as_state->current_content_hosting_configurations);
                                                ogs_list_init(as_state->current_content_hosting_configurations);

                                            } else {
                                                resource_id_node_t *next, *node;
                                                ogs_list_for_each_safe(as_state->current_content_hosting_configurations, next, node) {
                                                    ogs_free(node->state);
                                                    ogs_list_remove(as_state->current_content_hosting_configurations, node);
                                                    ogs_free(node);
                                                }
                                            }
                                            cJSON_ArrayForEach(entry, chc_array) {
                                                char *id = strrchr(entry->valuestring, '/');
                                                if (id == NULL) {
                                                    id = entry->valuestring;
                                                } else {
                                                    id++;
                                                }
                                                current_chc = ogs_calloc(1, sizeof(*current_chc));
                                                current_chc->state = ogs_strdup(id);
                                                ogs_debug("Adding [%s] to the current Content Hosting Configuration list",current_chc->state);
                                                ogs_list_add(as_state->current_content_hosting_configurations, current_chc);
                                            }

                                            cJSON_Delete(chc_array);
                                        }
                                        if (response->status == 500){
                                            ogs_error("Received Internal Server error\n");
                                        }
                                        if (response->status == 503) {
                                            ogs_error("Service Unavailable\n");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    DEFAULT
                                        ogs_error("Unknown M3 Content Hosting Configuratiobn GET operation [%s]", message.h.resource.component[1]);
                                        break;
                                END
                                break;
                            }
                            next_action_for_application_server(as_state);

                            break;

                        CASE("certificates")

                            msaf_application_server_state_node_t *as_state;
                            as_state = e->application_server_state;
                            ogs_assert(as_state);
                            if (message.h.resource.component[1]) {
                                SWITCH(message.h.method)
                                    CASE(OGS_SBI_HTTP_METHOD_POST)
                                        if(response->status == 201) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] recieved for certificate [%s]", message.h.resource.component[0], message.h.method, response->status, message.h.resource.component[1]);

                                            resource_id_node_t *certificate;

                                            //Iterate upload_certs and find match strcmp resource component 0
                                            ogs_list_for_each(&as_state->upload_certificates,certificate){
                                                if(!strcmp(certificate->state, message.h.resource.component[1]))
                                                    break;
                                            }
                                            if(certificate) {

                                                ogs_debug("Removing certificate [%s] from upload_certificates", certificate->state);

                                                ogs_list_remove(&as_state->upload_certificates, certificate);

                                                ogs_debug("Adding certificate [%s] to  current_certificates", certificate->state);

                                                ogs_list_add(as_state->current_certificates, certificate);
                                                // ogs_free(upload_cert_id);
                                            }
                                        }
                                        if(response->status == 405){
                                            ogs_error("Server Certificate resource already exist at the specified path\n");
                                        }
                                        if(response->status == 413){
                                            ogs_error("Payload too large\n");
                                        }
                                        if(response->status == 414){
                                            ogs_error("URI too long\n");
                                        }
                                        if(response->status == 415){
                                            ogs_error("Unsupported media type\n");
                                        }
                                        if(response->status == 500){
                                            ogs_error("Internal server error\n");
                                        }
                                        if(response->status == 503){
                                            ogs_error("Service unavailable\n");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    CASE(OGS_SBI_HTTP_METHOD_PUT)
                                        if(response->status == 200 || response->status == 204) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] recieved for certificate [%s]", message.h.resource.component[0], message.h.method, response->status,message.h.resource.component[1]);

                                            resource_id_node_t *certificate;

                                            msaf_application_server_state_log(&as_state->upload_certificates, "Upload Certificates");

                                            //Iterate upload_certs and find match strcmp resource component 0
                                            ogs_list_for_each(&as_state->upload_certificates,certificate){

                                                if(!strcmp(certificate->state, message.h.resource.component[1]))
                                                    break;
                                            }

                                            if(!certificate){
                                                ogs_debug("Certificate %s not found in upload certificates", message.h.resource.component[1]);
                                            } else {
                                                ogs_debug("Removing certificate [%s] from upload_certificates", certificate->state);
                                                ogs_free(certificate->state);

                                                ogs_list_remove(&as_state->upload_certificates, certificate);
                                                ogs_free(certificate);
                                            }
                                        }
                                        if(response->status == 404){
                                            ogs_error("Not Found\n");
                                        }
                                        if(response->status == 413){
                                            ogs_error("Payload too large\n");
                                        }
                                        if(response->status == 414){
                                            ogs_error("URI too long\n");
                                        }
                                        if(response->status == 415){
                                            ogs_error("Unsupported Media Type\n");
                                        }
                                        if(response->status == 500){
                                            ogs_error("Internal Server Error\n");
                                        }
                                        if(response->status == 503){
                                            ogs_error("Service Unavailable\n");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    CASE(OGS_SBI_HTTP_METHOD_DELETE)
                                        if(response->status == 204) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] recieved for Certificate [%s]", message.h.resource.component[0], message.h.method, response->status,message.h.resource.component[1]);

                                            resource_id_node_t *certificate, *next = NULL;
                                            resource_id_node_t *delete_certificate, *node = NULL;

                                            if(as_state->current_certificates) {
                                                ogs_list_for_each_safe(as_state->current_certificates, next, certificate){

                                                    if(!strcmp(certificate->state, message.h.resource.component[1]))
                                                        break;
                                                }
                                            }

                                            if(certificate) {

                                                msaf_application_server_state_log(as_state->current_certificates, "Current Certificates");

                                                ogs_debug("Removing certificate [%s] from current_certificates", certificate->state);
                                                ogs_free(certificate->state);

                                                ogs_list_remove(as_state->current_certificates, certificate);
                                                ogs_free(certificate);
                                                msaf_application_server_state_log(as_state->current_certificates, "Current Certificates");
                                            }


                                            ogs_list_for_each_safe(&as_state->delete_certificates, node, delete_certificate){

                                                if(!strcmp(delete_certificate->state, message.h.resource.component[1])) {
                                                    msaf_application_server_state_log(&as_state->delete_certificates, "Delete Certificates");

                                                    ogs_debug("Destroying Certificate: %s", delete_certificate->state);
                                                    ogs_free(delete_certificate->state);
                                                    ogs_list_remove(&as_state->delete_certificates, delete_certificate);
                                                    ogs_free(delete_certificate);
                                                    msaf_application_server_state_log(&as_state->delete_certificates, "Delete Certificates");

                                                }
                                            }
                                        }
                                        if(response->status == 404){
                                            ogs_error("Not Found\n");
                                        }
                                        if(response->status == 413){
                                            ogs_error("Payload too large\n");
                                        }
                                        if(response->status == 414){
                                            ogs_error("URI too long\n");
                                        }
                                        if(response->status == 415){
                                            ogs_error("Unsupported Media Type\n");
                                        }
                                        if(response->status == 500){
                                            ogs_error("Internal Server Error\n");
                                        }
                                        if(response->status == 503){
                                            ogs_error("Service Unavailable\n");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    DEFAULT
                                        ogs_error("Unknown M3 certificate operation [%s]", message.h.resource.component[1]);
                                        break;
                                END
                                break;
                            } else {
                                cJSON *entry;
                                cJSON *cert_array = cJSON_Parse(response->http.content);
                                resource_id_node_t *current_cert;
                                SWITCH(message.h.method)
                                    CASE(OGS_SBI_HTTP_METHOD_GET)

                                        if(response->status == 200) {

                                            ogs_debug("[%s] Method [%s] with Response [%d] received",
                                                    message.h.resource.component[0], message.h.method, response->status);

                                            if (as_state->current_certificates == NULL) {
                                                as_state->current_certificates = ogs_calloc(1,sizeof(*as_state->current_certificates));
                                                ogs_assert(as_state->current_certificates);
                                                ogs_list_init(as_state->current_certificates);

                                            } else {
                                                resource_id_node_t *next, *node;
                                                ogs_list_for_each_safe(as_state->current_certificates, next, node) {

                                                    ogs_debug("Removing certificate [%s] from current_certificates", node->state);

                                                    ogs_free(node->state);
                                                    ogs_list_remove(as_state->current_certificates, node);
                                                    ogs_free(node);
                                                }
                                            }
                                            cJSON_ArrayForEach(entry, cert_array) {
                                                char *id = strrchr(entry->valuestring, '/');
                                                if (id == NULL) {
                                                    id = entry->valuestring;
                                                } else {
                                                    id++;
                                                }
                                                current_cert = ogs_calloc(1, sizeof(*current_cert));
                                                current_cert->state = ogs_strdup(id);
                                                ogs_debug("Adding certificate [%s] to Current certificates", current_cert->state);
                                                ogs_list_add(as_state->current_certificates, current_cert);
                                            }

                                            cJSON_Delete(cert_array);
                                        }
                                        if (response->status == 500){
                                            ogs_error("Received Internal Server error");
                                        }
                                        if (response->status == 503) {
                                            ogs_error("Service Unavailable");
                                        }
                                        next_action_for_application_server(as_state);
                                        break;
                                    DEFAULT
                                        ogs_error("Unknown M3 certificate GET operation [%s]", message.h.resource.component[1]);
                                        break;
                                END
                                break;
                            }
                            next_action_for_application_server(as_state);

                            break;

                        DEFAULT
                            ogs_error("Unknown M3 operation [%s]", message.h.resource.component[0]);
                            break;
                    END
                    break;

                CASE(OGS_SBI_SERVICE_NAME_NNRF_NFM)

                    SWITCH(message.h.resource.component[0])
                        CASE(OGS_SBI_RESOURCE_NAME_NF_INSTANCES)
                            nf_instance = e->h.sbi.data;
                            ogs_assert(nf_instance);
                            ogs_assert(OGS_FSM_STATE(&nf_instance->sm));

                            e->h.sbi.message = &message;
                            ogs_fsm_dispatch(&nf_instance->sm, e);
                            break;

                        CASE(OGS_SBI_RESOURCE_NAME_SUBSCRIPTIONS)
                            subscription_data = e->h.sbi.data;
                            ogs_assert(subscription_data);

                            SWITCH(message.h.method)
                                CASE(OGS_SBI_HTTP_METHOD_POST)
                                    if (message.res_status == OGS_SBI_HTTP_STATUS_CREATED ||
                                            message.res_status == OGS_SBI_HTTP_STATUS_OK) {
                                        ogs_nnrf_nfm_handle_nf_status_subscribe(
                                                subscription_data, &message);
                                    } else {
                                        ogs_error("HTTP response error : %d",
                                                message.res_status);
                                    }
                                    break;

                                CASE(OGS_SBI_HTTP_METHOD_DELETE)
                                    if (message.res_status == OGS_SBI_HTTP_STATUS_NO_CONTENT) {
                                        ogs_sbi_subscription_data_remove(subscription_data);
                                    } else {
                                        ogs_error("HTTP response error : %d",
                                                message.res_status);
                                    }
                                    break;

                                DEFAULT
                                    ogs_error("Invalid HTTP method [%s]", message.h.method);
                                    ogs_assert_if_reached();
                            END
                            break;

                        DEFAULT
                            ogs_error("Invalid resource name [%s]",
                                    message.h.resource.component[0]);
                            ogs_assert_if_reached();
                    END
                    break;

                DEFAULT
                    ogs_error("Invalid service name [%s]", message.h.service.name);
                    ogs_assert_if_reached();
            END

            ogs_sbi_message_free(&message);
            ogs_sbi_response_free(response);
            break;

        case OGS_EVENT_SBI_TIMER:
            ogs_assert(e);

            switch(e->h.timer_id) {
                case OGS_TIMER_NF_INSTANCE_REGISTRATION_INTERVAL:
                case OGS_TIMER_NF_INSTANCE_HEARTBEAT_INTERVAL:
                case OGS_TIMER_NF_INSTANCE_NO_HEARTBEAT:
                case OGS_TIMER_NF_INSTANCE_VALIDITY:
                    nf_instance = e->h.sbi.data;
                    ogs_assert(nf_instance);
                    ogs_assert(OGS_FSM_STATE(&nf_instance->sm));

                    ogs_fsm_dispatch(&nf_instance->sm, e);
                    if (OGS_FSM_CHECK(&nf_instance->sm, ogs_sbi_nf_state_exception))
                        ogs_error("State machine exception [%d]", e->h.timer_id);
                    break;

                case OGS_TIMER_SUBSCRIPTION_VALIDITY:
                    subscription_data = e->h.sbi.data;
                    ogs_assert(subscription_data);

                    ogs_assert(true ==
                            ogs_nnrf_nfm_send_nf_status_subscribe(subscription_data));

                    ogs_debug("Subscription validity expired [%s]",
                            subscription_data->id);
                    ogs_sbi_subscription_data_remove(subscription_data);
                    break;

                case OGS_TIMER_SBI_CLIENT_WAIT:
                    sbi_xact = e->h.sbi.data;
                    ogs_assert(sbi_xact);

                    stream = sbi_xact->assoc_stream;

                    ogs_sbi_xact_remove(sbi_xact);

                    ogs_error("Cannot receive SBI message");
                    if (stream) {
                        ogs_assert(true ==
                                ogs_sbi_server_send_error(stream,
                                    OGS_SBI_HTTP_STATUS_GATEWAY_TIMEOUT, NULL,
                                    "Cannot receive SBI message", NULL));
                    }
                    break;

                default:
                    ogs_error("Unknown timer[%s:%d]",
                            ogs_timer_get_name(e->h.timer_id), e->h.timer_id);
            }
            break;

        default:
            ogs_error("No handler for event %s", msaf_event_get_name(e));
            break;
    }
}

