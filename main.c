#include "stdio.h"
#include "libwebsockets.h"
//#include "json.h" // TODO: https://github.com/json-c/json-c/blob/json-c-0.14-20200419/tests/test2.c

static struct my_conn
{
    lws_sorted_usec_list_t sul; /* schedule connection retry */
    struct lws *wsi;            /* related wsi if any */
    uint16_t retry_count;       /* count of consequetive retries */
} mco;

static struct lws_context *context;
static int interrupted;
static int port = 443;
static int ssl_connection = LCCSCF_USE_SSL;
static const char *server_address = "libwebsockets.org";
static const char *pro = "dumb-increment-protocol";

static const uint32_t backoff_ms[] = {1000, 2000, 3000, 4000, 5000};

static const lws_retry_bo_t retry = {
    .retry_ms_table = backoff_ms,
    .retry_ms_table_count = LWS_ARRAY_SIZE(backoff_ms),
    .conceal_count = LWS_ARRAY_SIZE(backoff_ms),

    .secs_since_valid_ping = 3,    /* force PINGs after secs idle */
    .secs_since_valid_hangup = 10, /* hangup after secs idle */

    .jitter_percent = 20,
};

static void connect_client(lws_sorted_usec_list_t *sul)
{
    struct my_conn *mco = lws_container_of(sul, struct my_conn, sul);
    struct lws_client_connect_info i;

    memset(&i, 0, sizeof(i));

    i.context = context;
    i.port = port;
    i.address = server_address;
    i.path = "/";
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = ssl_connection;
    i.protocol = pro;
    i.local_protocol_name = "discord-bot-test";
    i.pwsi = &mco->wsi;
    i.retry_and_idle_policy = &retry;
    i.userdata = mco;

    if (!lws_client_connect_via_info(&i))
        /*
		 * Failed... schedule a retry... we can't use the _retry_wsi()
		 * convenience wrapper api here because no valid wsi at this
		 * point.
		 */
        if (lws_retry_sul_schedule(context, 0, sul, &retry, connect_client, &mco->retry_count))
        {
            lwsl_err("%s: connection attempts exhausted\n", __func__);
            interrupted = 1;
        }
}

static int callback_minimal(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    struct my_conn *mco = (struct my_conn *)user;

    switch (reason)
    {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        lwsl_err("CLIENT_CONNECTION_ERROR: %s\n", in ? (char *)in : "(null)");
        goto do_retry;
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
        lwsl_hexdump_notice(in, len);
        break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lwsl_user("%s: established\n", __func__);
        break;

    case LWS_CALLBACK_CLIENT_CLOSED:
        goto do_retry;

    default:
        break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);

do_retry:
    /*
	 * retry the connection to keep it nailed up
	 *
	 * For this example, we try to conceal any problem for one set of
	 * backoff retries and then exit the app.
	 *
	 * If you set retry.conceal_count to be larger than the number of
	 * elements in the backoff table, it will never give up and keep
	 * retrying at the last backoff delay plus the random jitter amount.
	 */
    if (lws_retry_sul_schedule_retry_wsi(wsi, &mco->sul, connect_client, &mco->retry_count))
    {
        lwsl_err("%s: connection attempts exhausted\n", __func__);
        exit(1);
    }

    return 0;
}

static const struct lws_protocols protocols[] = {
    {
        "discord-bot-test",
        callback_minimal,
        0,
        0,
    },
    {NULL, NULL, 0, 0},
};

void main()
{
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof info);
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.fd_limit_per_thread = 3;

    context = lws_create_context(&info);
    if (!context)
    {
        lwsl_err("lws init failed\n");
        exit(1);
    }

    int n = 0;
    lws_sul_schedule(context, 0, &mco.sul, connect_client, 1);
    while (n >= 0 && !interrupted)
    {
        n = lws_service(context, 0);
    }

    lws_context_destroy(context);
    lwsl_user("Completed\n");

    return;
}
