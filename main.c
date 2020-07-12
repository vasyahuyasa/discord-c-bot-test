#include <curl/curl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "auth.h"
#include <time.h>

#define APPLICATION_INFO_URL "https://discord.com/api/oauth2/applications/@me"
#define GET_CHANNELS_URL "https://discord.com/api/channels"

enum
{
    STATE_NONE = 0,
    STATE_GETTING_CHANNEL_INFO,
    STATE_WORKING,
    STATE_ERROR,
};

struct string
{
    char *ptr;
    size_t len;
};

struct message
{
    char *from;
    char *content;
};

struct channel
{
    char *last_message_id;
}

init_string(struct string *s)
{
    s->len = 0;
    s->ptr = malloc(s->len + 1);
    if (s->ptr == NULL)
    {
        fprintf(stderr, "malloc() failed\n");
        exit(1);
    }
    s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s)
{
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len + 1);
    if (s->ptr == NULL)
    {
        fprintf(stderr, "realloc() failed\n");
        exit(1);
    }
    memcpy(s->ptr + s->len, ptr, size * nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;

    return size * nmemb;
}

struct message *get_messags_reponse_to_list(const char *json_str, size_t *len)
{
    json_object *messages_list;
    messages_list = json_tokener_parse(json_str);

    *len = json_object_array_length(messages_list);
    if (*len == 0)
    {
        return NULL;
    }

    struct message *messages;
    messages = (struct message *)malloc(sizeof(struct message));

    for (int i = 0; i < *len; i++)
    {
        json_object *msg_obj, *message_author, *message_author_name, *message_content;
        msg_obj = json_object_array_get_idx(messages_list, i);

        message_author = json_object_object_get(msg_obj, "author");
        message_author_name = json_object_object_get(message_author, "username");
        message_content = json_object_object_get(msg_obj, "content");

        char *str_author_name;
        char *str_content;
        int author_name_len = json_object_get_string_len(message_author_name);
        int content_len = json_object_get_string_len(message_content);
        str_author_name = malloc(author_name_len + 1);
        str_content = malloc(content_len + 1);

        strcpy(str_author_name, json_object_get_string(message_author_name));
        strcpy(str_content, json_object_get_string(message_content));

        struct message msg = {str_author_name, str_content};
        messages[i] = msg;
    };

    return messages;
}

void get_channel_info(const char *json_str, struct channel *chan)
{
    json_object *channel_obj = json_tokener_parse(json_str);
    json_object *last_msg_obj = json_object_object_get(channel_obj, "last_message_id");
    chan->last_message_id = json_object_get_string(last_msg_obj);
}

#define MAX_REQUESTS 50

int state = STATE_NONE;
CURLM *multi_handle;
char *last_message_id;
char *auth_header;
char *channel_info_url;
char *channel_messages_url;
struct string channel_info_data = {NULL, 0};
struct timespec last_message_time = {0, 0};
struct discord_request *all_requests[MAX_REQUESTS];
int running_queries = 0;

#define CHECKINTERVAL_US (1000 * 500)

typedef void (*result_cb)(CURLcode, struct string *);

struct discord_request
{
    CURL *handle;
    result_cb *cb;
};

size_t run_request(struct discord_request *req)
{
    for (size_t i = 0; i < MAX_REQUESTS; i++)
    {
        if (all_requests[i] == NULL)
        {
            CURLMcode code = curl_multi_add_handle(multi_handle, req->handle);
            if (code != CURLM_OK) {
                printf("can not add ")
            }
            all_requests[i] = req;
            running_queries++;
            return i;
        }
    }

    printf("can not enqueue query: queue is full");
}

void discord_init()
{
    memset(all_requests, NULL, sizeof(struct discord_request *) * MAX_REQUESTS);

    channel_info_url = malloc(100);
    auth_header = malloc(100);
    channel_messages_url = malloc(100);
    memset(channel_info_url, 0, 100);
    memset(auth_header, 0, 100);
    memset(channel_messages_url, 0, 100);

    sprintf(channel_info_url, "https://discord.com/api/channels/%s", CHANNEL_ID);
    sprintf(auth_header, "Authorization: Bot %s", TOKEN);
    sprintf(channel_messages_url, "https://discord.com/api/channels/%s/messages", CHANNEL_ID);

    multi_handle = curl_multi_init();
}

void process_channel_info(CURLcode code, struct string *result)
{
}

void discord_run()
{
    if (running_queries > 0) {

    }

    switch (state)
    {
    case STATE_NONE:
    {
        CURL *handle = curl_easy_init();
        struct curl_slist *header = curl_slist_append(NULL, auth_header);
        struct string *buf = malloc(sizeof(struct string));

        init_string(buf);

        curl_easy_setopt(handle, CURLOPT_URL, channel_info_url);
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, buf);

        struct discord_request *req = malloc(sizeof(struct discord_request));
        req->handle = handle;
        req->cb = process_channel_info;

        run_request(req);
    }

    case STATE_GETTING_CHANNEL_INFO:
    {
        CURLMcode mc;
        int pending;

        mc = curl_multi_perform(multi_handle, &pending);
        if (mc == CURLM_OK)
        {
            mc = curl_multi_poll(multi_handle, NULL, 0, 1, &pending);
        }

        if (mc != CURLM_OK)
        {
            fprintf(stderr, "curl_multi failed, code %d.\n", mc);
            state = STATE_ERROR;
            break;
        }

        if (pending != 0)
        {
            break;
        }

        CURLMsg *msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(multi_handle, &msgs_left)))
        {
            if (msg->msg != CURLMSG_DONE)
            {
                continue;
            }

            if (msg->data.result != CURLE_OK)
            {
                printf("HTTP transfer completed with status %d", msg->data.result);
                break;
            }

            struct channel *chan = malloc(sizeof(struct channel));
            get_channel_info(channel_info_data.ptr, chan);
            last_message_id = chan->last_message_id;

            printf("last_message_id=%s\n", last_message_id);
            state = STATE_WORKING;
        }
        break;
    }

    case STATE_WORKING:
    {

        exit(0);
        break;
    }
    }
}

int main()
{
    discord_init();

    while (1)
    {
        discord_run();
    }
}
