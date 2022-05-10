#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "sysrepo.h"
#include "sysrepo/xpath.h"

#include "shash.h"
#include "log.h"

#include "dynamic-string.h"
#include "lldp.h"
#include "interface.h"
#include "bridge.h"
#include "hardware.h"
#include "dbus_util.h"
#include "hiredis/hiredis.h"

static sr_subscription_ctx_t *subscription = NULL;

volatile int exit_application = 0;
struct shash interfaces;


static int provider_cb(
    sr_session_ctx_t *session, uint32_t sub_id, const char *module_name, const char *xpath,
    const char *request_xpath, uint32_t request_id, struct lyd_node **parent, void *private_data)
{
    log_info("Get request path: %s\n", xpath);
    if (strcmp(module_name, "ietf-interfaces") == 0) {
        if (strcmp(xpath, "/ietf-interfaces:interfaces/interface/statistics") == 0) {
            interface_statistics_provider(session, parent);
        } else if (strcmp(xpath, "/ietf-interfaces:interfaces/interface/oper-status") == 0) {
            interface_oper_status_provider(session, parent);
        }
    } else if (strcmp(module_name, "ieee802-dot1ab-lldp") == 0) {
        if (strcmp(xpath, "/ieee802-dot1ab-lldp:lldp/port") == 0) {
            lldp_port_provider(session, parent);
        }
    }

    return SR_ERR_OK;
}

static void
print_val(const sr_val_t *value)
{
    if (NULL == value) {
        return;
    }

    printf("%s ", value->xpath);

    switch (value->type) {
    case SR_CONTAINER_T:
    case SR_CONTAINER_PRESENCE_T:
        printf("(container)");
        break;
    case SR_LIST_T:
        printf("(list instance)");
        break;
    case SR_STRING_T:
        printf("= %s", value->data.string_val);
        break;
    case SR_BOOL_T:
        printf("= %s", value->data.bool_val ? "true" : "false");
        break;
    case SR_DECIMAL64_T:
        printf("= %g", value->data.decimal64_val);
        break;
    case SR_INT8_T:
        printf("= %" PRId8, value->data.int8_val);
        break;
    case SR_INT16_T:
        printf("= %" PRId16, value->data.int16_val);
        break;
    case SR_INT32_T:
        printf("= %" PRId32, value->data.int32_val);
        break;
    case SR_INT64_T:
        printf("= %" PRId64, value->data.int64_val);
        break;
    case SR_UINT8_T:
        printf("= %" PRIu8, value->data.uint8_val);
        break;
    case SR_UINT16_T:
        printf("= %" PRIu16, value->data.uint16_val);
        break;
    case SR_UINT32_T:
        printf("= %" PRIu32, value->data.uint32_val);
        break;
    case SR_UINT64_T:
        printf("= %" PRIu64, value->data.uint64_val);
        break;
    case SR_IDENTITYREF_T:
        printf("= %s", value->data.identityref_val);
        break;
    case SR_INSTANCEID_T:
        printf("= %s", value->data.instanceid_val);
        break;
    case SR_BITS_T:
        printf("= %s", value->data.bits_val);
        break;
    case SR_BINARY_T:
        printf("= %s", value->data.binary_val);
        break;
    case SR_ENUM_T:
        printf("= %s", value->data.enum_val);
        break;
    case SR_LEAF_EMPTY_T:
        printf("(empty leaf)");
        break;
    default:
        printf("(unprintable)");
        break;
    }

    switch (value->type) {
    case SR_UNKNOWN_T:
    case SR_CONTAINER_T:
    case SR_CONTAINER_PRESENCE_T:
    case SR_LIST_T:
    case SR_LEAF_EMPTY_T:
        printf("\n");
        break;
    default:
        printf("%s\n", value->dflt ? " [default]" : "");
        break;
    }
}

static void
print_change(sr_change_oper_t op, sr_val_t *old_val, sr_val_t *new_val)
{
    switch (op) {
    case SR_OP_CREATED:
        printf("CREATED: ");
        print_val(new_val);
        break;
    case SR_OP_DELETED:
        printf("DELETED: ");
        print_val(old_val);
        break;
    case SR_OP_MODIFIED:
        printf("MODIFIED: ");
        print_val(old_val);
        printf("to ");
        print_val(new_val);
        break;
    case SR_OP_MOVED:
        printf("MOVED: %s\n", new_val->xpath);
        break;
    }
}

/* TODO: apply config to our driver ???
 *
 */
int cb_cfg_change(sr_session_ctx_t *session, uint32_t sub_id, const char *module_name,
                  const char *xpath, sr_event_t event, uint32_t request_id, void *private_data)
{
    sr_change_iter_t *it = NULL;
    sr_val_t *old_value = NULL;
    sr_val_t *new_value = NULL;
    int rc = SR_ERR_OK;
    sr_change_oper_t oper;
    char path[512];

    log_info("Get cfg change path/event: %s/%d\n", xpath, event);

    if (xpath) {
        sprintf(path, "%s//.", xpath);
    } else {
        sprintf(path, "/%s:*//.", module_name);
    }

    dbus_query(path);

    rc = sr_get_changes_iter(session, path, &it);
    if (rc != SR_ERR_OK) {
        goto cleanup;
    }

    while ((rc = sr_get_change_next(session, it, &oper, &old_value, &new_value)) == SR_ERR_OK) {
        print_change(oper, old_value, new_value);
        sr_free_val(old_value);
        sr_free_val(new_value);
    }

cleanup:
    sr_free_change_iter(it);
    return SR_ERR_OK;

#if 0 //TODO: return SR_ERR_VALIDATION_FAILED to abort config change
    printf("event - %d\n", event);
    if (event == SR_EV_CHANGE) {
        return SR_ERR_VALIDATION_FAILED;
    }
#endif
}

static int data_provider(sr_session_ctx_t *session)
{
    int rc = SR_ERR_OK;

#if 1
    rc = sr_module_change_subscribe(session, "ietf-interfaces", NULL, cb_cfg_change,
                                    NULL, 0, SR_SUBSCR_DEFAULT, &subscription);
    if (SR_ERR_OK == rc) {
        printf("[%s] sr_module_change_subscribe() ok.\n", __func__);
    } else {
        printf("[%s] sr_module_change_subscribe() error: rc(%d).\n", __func__, rc);
    }
#endif

    rc = sr_oper_get_subscribe(session, "ietf-interfaces", "/ietf-interfaces:interfaces/interface/statistics",
                               provider_cb, NULL, SR_SUBSCR_DEFAULT, &subscription);

    rc = sr_oper_get_subscribe(session, "ietf-interfaces", "/ietf-interfaces:interfaces/interface/oper-status",
                               provider_cb, NULL, SR_SUBSCR_DEFAULT, &subscription);

    rc = sr_oper_get_subscribe(session, "ieee802-dot1ab-lldp", "/ieee802-dot1ab-lldp:lldp/port",
                               provider_cb, NULL, SR_SUBSCR_DEFAULT, &subscription);

    while (!exit_application) {
        sleep(1);
        update_ips(&interfaces, session);
        update_interfaces_speed(&interfaces, session);
    }

cleanup:
    if (NULL != subscription) {
        sr_unsubscribe(subscription);
    }

    return rc;
}

void* child(void* data) {
    char *str = (char*) data;

    for (int i = 0;i < 3;++i) {
        printf("%s\n", str);
        sleep(1);
    }

    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    struct shash_node *node, *node_next;
    struct interface *intf;
    sr_conn_ctx_t *connection = NULL;
    sr_session_ctx_t *session = NULL;
    int rc = SR_ERR_OK;

    pthread_t t;
    pthread_create(&t, NULL, child, "Child");

    log_set_level(LOG_INFO);

    shash_init(&interfaces);

    // set sysrepo's log level
    sr_log_stderr(SR_LL_WRN);

    rc = sr_connect(SR_CONN_DEFAULT, &connection);
    if (rc != SR_ERR_OK) {
        log_fatal("Connection to sysrepo failed: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    rc = sr_session_start(connection, SR_DS_RUNNING, &session);
    if (rc != SR_ERR_OK) {
        log_fatal("Get session from sysrepo's connection failed: %s\n", sr_strerror(rc));
        goto cleanup;
    }

    rc = sr_delete_item(session, "/ieee802-dot1ab-lldp:lldp", SR_EDIT_DEFAULT);
    if (rc != SR_ERR_OK) {
        log_error("Delete lldp from sysrepo failed: %s", sr_strerror(rc));
    }

    rc = sr_apply_changes(session, 0);
    if (rc != SR_ERR_OK) {
        log_error("Delete lldp from sysrepo apply failed: %s", sr_strerror(rc));
    }

    // delete all interfaces in sysrepo
    rc = sr_delete_item(session, "/ietf-interfaces:interfaces", SR_EDIT_DEFAULT);
    if (rc != SR_ERR_OK) {
        log_error("Delete interface from sysrepo failed: %s", sr_strerror(rc));
    }
    rc = sr_apply_changes(session, 0);
    if (rc != SR_ERR_OK) {
        log_error("Delete interface from sysrepo apply failed: %s", sr_strerror(rc));
    }

    // collect interfaces' info
#if 1
    collect_interfaces(&interfaces);
    SHASH_FOR_EACH(node, &interfaces) {
        intf = (struct interface*)node->data;
        save_interface_running(intf, session);
    }
#endif

    sr_session_switch_ds(session, SR_DS_OPERATIONAL);
    SHASH_FOR_EACH(node, &interfaces) {
        intf = (struct interface*)node->data;
        save_interface_operational(intf, session);
    }
    sr_session_switch_ds(session, SR_DS_RUNNING);

    save_hardware_chassis(session);
    update_ips(&interfaces, session);

    struct shash bridges;
    struct shash_node *br_node = NULL;
    shash_init(&bridges);
    collect_bridges(&bridges);
    save_bridges(&bridges, session);
    shash_destroy_free_data(&bridges);

    /* sample output :
        !!! REDIS OK
        Result: admin_status = up
        Result: alias = Eth26/1(Port26)
        Result: index = 26
        Result: lanes = 54
        Result: mtu = 9100
        Result: speed = 10000
        Result: parent_port = Ethernet25
        Result: description =
        Result: oper_status = up
        Result: autoneg = off
        Result: oper_speed = 10000
        Result: fec = none
    */

    {
        redisContext    *c;
        int             is_redis_ok = 0;
        struct timeval timeout = { 1, 500000 }; // 1.5 seconds

        c = redisConnectWithTimeout("192.168.40.155", 63795, timeout);
        if (c == NULL || c->err) {
            if (c) {
                printf("Connection error: %s\n", c->errstr);
                redisFree(c);
            } else {
                printf("Connection error: can't allocate redis context\n");
            }
        }
        else {
            is_redis_ok = 1;
            printf("!!! REDIS OK\n");
        }

        if (is_redis_ok) {
            redisReply *reply = redisCommand(c, "HGETALL %s", "PORT_TABLE:Ethernet25");
            if ( reply->type == REDIS_REPLY_ERROR ) {
                printf( "Error: %s\n", reply->str );
            } else if ( reply->type != REDIS_REPLY_ARRAY ) {
                printf( "Unexpected type: %d\n", reply->type );
            } else {
                int i;
                for (i = 0; i < reply->elements; i = i + 2 ) {
                    printf( "Result: %s = %s \n", reply->element[i]->str, reply->element[i + 1]->str );
                }
            }
            freeReplyObject(reply);
        }
    }

    rc = data_provider(session);

cleanup:
    sr_disconnect(connection);

    SHASH_FOR_EACH_SAFE(node, node_next, &interfaces) {
        interface_destroy((struct interface*)node->data);
    }
    shash_destroy(&interfaces);

    destroy_interface_names();

    pthread_join(t, NULL);

    return 0;
}

