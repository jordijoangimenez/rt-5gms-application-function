 /*
License: 5G-MAG Public License (v1.0)
Author: Vuk Stojkovic
Copyright: (C) 2023-2024 Fraunhofer FOKUS

For full license terms please see the LICENSE file distributed with this
program. If this file is missing then the license can be retrieved from
https://drive.google.com/file/d/1cinCiA778IErENZ3JN52VFW-1ffHpx7Z/view
*/

#include "ogs-core.h"
#include "hash.h"
#include "utilities.h"
#include "provisioning-session.h"
#include "metrics-reporting-configuration.h"


static char *calculate_metrics_reporting_configuration_hash(msaf_api_metrics_reporting_configuration_t *metricsReportingConfiguration);

ogs_hash_t * msaf_metrics_reporting_map(void)
{
    ogs_hash_t *metrics_reporting_map = ogs_hash_make();
    return metrics_reporting_map;
}

static char *calculate_metrics_reporting_configuration_hash(msaf_api_metrics_reporting_configuration_t *metricsReportingConfiguration)
{
    cJSON *metrics_reporting_config = NULL;
    char *metricsReportingConfiguration_to_hash;
    char *metricsReportingConfiguration_hashed = NULL;
    metrics_reporting_config = msaf_api_metrics_reporting_configuration_convertToJSON(metricsReportingConfiguration, false);
    metricsReportingConfiguration_to_hash = cJSON_Print(metrics_reporting_config);
    cJSON_Delete(metrics_reporting_config);
    metricsReportingConfiguration_hashed = calculate_hash(metricsReportingConfiguration_to_hash);
    cJSON_free(metricsReportingConfiguration_to_hash);
    return metricsReportingConfiguration_hashed;
}

 msaf_metrics_reporting_configuration_t* process_and_map_metrics_reporting_configuration(msaf_provisioning_session_t *provisioning_session, msaf_api_metrics_reporting_configuration_t *parsed_config) {

     ogs_assert(provisioning_session);
     ogs_assert(parsed_config);

     ogs_uuid_t uuid;
     ogs_uuid_get(&uuid);
     char new_id[OGS_UUID_FORMATTED_LENGTH + 1];
     ogs_uuid_format(new_id, &uuid);

     if (parsed_config->metrics_reporting_configuration_id != NULL) {
         ogs_free(parsed_config->metrics_reporting_configuration_id);
     }
     parsed_config->metrics_reporting_configuration_id = msaf_strdup(new_id);

     msaf_metrics_reporting_configuration_t *msaf_metrics_config = ogs_calloc(1, sizeof(msaf_metrics_reporting_configuration_t));

     if (!msaf_metrics_config) {
         ogs_error("Failed to allocate msaf_metrics_reporting_configuration");
         return NULL;
     }

     msaf_metrics_config->config = parsed_config;
     msaf_metrics_config->etag = calculate_metrics_reporting_configuration_hash(msaf_metrics_config->config);
     msaf_metrics_config->receivedTime = time(NULL);

     if (provisioning_session->metrics_reporting_map == NULL) {
         provisioning_session->metrics_reporting_map = msaf_metrics_reporting_map();
     }

     char *hashKey = msaf_strdup(msaf_metrics_config->config->metrics_reporting_configuration_id);
     ogs_hash_set(provisioning_session->metrics_reporting_map, hashKey, OGS_HASH_KEY_STRING, msaf_metrics_config);

     return msaf_metrics_config;
 }

 msaf_metrics_reporting_configuration_t* msaf_metrics_reporting_configuration_retrieve(const msaf_provisioning_session_t *provisioning_session, const char *metrics_configuration_id) {
     if (!provisioning_session || !metrics_configuration_id) {
         return NULL;
     }
     return (msaf_metrics_reporting_configuration_t*)ogs_hash_get(provisioning_session->metrics_reporting_map, metrics_configuration_id, OGS_HASH_KEY_STRING);
 }

 cJSON *msaf_metrics_reporting_configuration_convertToJSON(const msaf_metrics_reporting_configuration_t *msaf_metrics_reporting_configuration) {

     if (msaf_metrics_reporting_configuration == NULL || msaf_metrics_reporting_configuration->config == NULL) {
         ogs_error("msaf_metrics_reporting_configuration_convertToJSON() failed [NULL pointer]");
         return NULL;
     }

     cJSON *item = cJSON_CreateObject();

     cJSON *configJSON = msaf_api_metrics_reporting_configuration_convertToJSON(msaf_metrics_reporting_configuration->config, false);
     if (configJSON == NULL) {
         ogs_error("msaf_metrics_reporting_configuration_convertToJSON() failed [config conversion]");
         cJSON_Delete(item);
         return NULL;
     }
     cJSON_AddItemToObject(item, "config", configJSON);

     if (msaf_metrics_reporting_configuration->etag) {
         if (cJSON_AddStringToObject(item, "etag", msaf_metrics_reporting_configuration->etag) == NULL) {
             ogs_error("msaf_metrics_reporting_configuration_convertToJSON() failed [etag]");
             cJSON_Delete(item);
             return NULL;
         }
     }

     if (cJSON_AddNumberToObject(item, "receivedTime", (double)msaf_metrics_reporting_configuration->receivedTime) == NULL) {
         ogs_error("msaf_metrics_reporting_configuration_convertToJSON() failed [receivedTime]");
         cJSON_Delete(item);
         return NULL;
     }

     return item;
 }
