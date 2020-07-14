#include <curl/curl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <time.h>
#include "auth.h"

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
    char *id;
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

struct message *response_to_messages(const char *json_str, size_t *len)
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
        json_object *msg_obj = json_object_array_get_idx(messages_list, i);

        json_object *message_author = json_object_object_get(msg_obj, "author");
        json_object *message_author_name = json_object_object_get(message_author, "username");
        json_object *message_content = json_object_object_get(msg_obj, "content");
        json_object *message_id = json_object_object_get(msg_obj, "id");

        char *str_author_name = malloc(json_object_get_string_len(message_author_name) + 1);
        char *str_content = malloc(json_object_get_string_len(message_content) + 1);
        char *str_id = malloc(json_object_get_string_len(message_id) + 1);

        strcpy(str_author_name, json_object_get_string(message_author_name));
        strcpy(str_content, json_object_get_string(message_content));
        strcpy(str_id, json_object_get_string(message_id));

        struct message msg = {str_author_name, str_content, str_id};
        messages[*len - 1 - i] = msg;
    };

    return messages;
}

void get_channel_info(const char *json_str, struct channel *chan)
{
    json_object *channel_obj = json_tokener_parse(json_str);
    json_object *last_msg_obj = json_object_object_get(channel_obj, "last_message_id");

    size_t len = json_object_get_string_len(last_msg_obj);
    chan->last_message_id = malloc(sizeof(char) * len + 1);
    strcpy(chan->last_message_id, json_object_get_string(last_msg_obj));
    chan->last_message_id[len] = 0;
}

#define MAX_REQUESTS 50

CURLM *multi_handle;
char *last_message_id;
char *auth_header;
char *channel_info_url;
char *channel_messages_url;
struct string channel_info_data = {NULL, 0};
struct timespec next_message_check = {0, 0};
struct discord_request *all_requests[MAX_REQUESTS];
int running_queries = 0;
int got_channel_info = 0;

#define CHECKINTERVAL_US (1000 * 500)

typedef void(result_cb)(long, struct string *);

struct discord_request
{
    CURL *handle;
    result_cb *cb;
    struct string *data;
};

size_t run_request(struct discord_request *req)
{
    for (size_t i = 0; i < MAX_REQUESTS; i++)
    {
        if (all_requests[i] == NULL)
        {
            CURLMcode code = curl_multi_add_handle(multi_handle, req->handle);
            if (code != CURLM_OK)
            {
                printf("can not add handle: code %d\n", code);
                return -1;
            }

            all_requests[i] = req;
            running_queries++;
            return i;
        }
    }

    printf("can not enqueue query: queue is full\n");
}

void discord_init()
{
    channel_info_url = malloc(100);
    auth_header = malloc(100);
    channel_messages_url = malloc(100);
    memset(channel_info_url, 0, 100);
    memset(auth_header, 0, 100);
    memset(channel_messages_url, 0, 100);
    memset(all_requests, 0, sizeof(struct discord_request *) * MAX_REQUESTS);

    sprintf(channel_info_url, "https://discord.com/api/v6/channels/%s", CHANNEL_ID);
    sprintf(auth_header, "Authorization: Bot %s", TOKEN);
    sprintf(channel_messages_url, "https://discord.com/api/v6/channels/%s/messages", CHANNEL_ID);

    clock_gettime(CLOCK_MONOTONIC, &next_message_check);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    multi_handle = curl_multi_init();
}

void process_create_message(long response_code, struct string *data)
{
    printf("message created: %s\n", data->ptr);
}
void request_create_message(const char *text)
{
    CURL *handle = curl_easy_init();
    struct curl_slist *header = curl_slist_append(NULL, auth_header);
    header = curl_slist_append(header, "Content-Type: application/json");
    struct string *buf = malloc(sizeof(struct string));
    char *body = malloc(sizeof(char) * 2000);

    sprintf(body, "{\"content\":\"%s\", \"tts\": false}", text);
    long body_len = strlen(body);

    init_string(buf);

    curl_easy_setopt(handle, CURLOPT_URL, channel_messages_url);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, body_len);
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    struct discord_request *req = malloc(sizeof(struct discord_request));
    req->handle = handle;
    req->cb = &process_create_message;
    req->data = buf;

    run_request(req);

    next_message_check.tv_sec++;
}

void process_channel_info(long response_code, struct string *data)
{
    if (response_code != 200)
    {
        printf("process_channel_info code %d\n%s\n", response_code, data->ptr);
        return;
    }

    struct channel *ch_info = malloc(sizeof(struct channel));
    get_channel_info(data->ptr, ch_info);

    last_message_id = ch_info->last_message_id;
    got_channel_info = 1;
}

void process_last_messages(long response_code, struct string *data)
{
    printf("%s\n", data->ptr);

    if (response_code != 200)
    {
        printf("process_last_messages code %d\n%s\n", response_code, data->ptr);
        return;
    }

    size_t len;
    struct message *new_messages = response_to_messages(data->ptr, &len);
    if (len == 0)
    {
        return;
    }

    printf("process after %d\n", len);

    char *msg_id;
    for (size_t i = 0; i < len; i++)
    {
        printf("%s: %s (%s)\n", new_messages[i].from, new_messages[i].content, new_messages[i].id);
        msg_id = new_messages[i].id;

        char *echo_text = malloc(300);
        sprintf(echo_text, "Echo: %s", new_messages[i].content);
        request_create_message(echo_text);
    }

    last_message_id = msg_id;
    next_message_check.tv_sec++;
}

void request_new_messages_after(const char *msg_id)
{
    CURL *handle = curl_easy_init();
    if (handle == NULL)
    {
        printf("can not init CURL\n");
        exit(1);
    }

    char *url = malloc(sizeof(char) * 150);
    struct curl_slist *header = curl_slist_append(NULL, auth_header);
    struct string *buf = malloc(sizeof(struct string));
    sprintf(url, "%s?after=%s&limit=5", channel_messages_url, msg_id);

    printf("request after %s\n", msg_id);

    init_string(buf);

    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    struct discord_request *req = malloc(sizeof(struct discord_request));
    req->handle = handle;
    req->cb = &process_last_messages;
    req->data = buf;

    run_request(req);

    next_message_check.tv_sec++;
}

void request_last_message_id()
{
    CURL *handle = curl_easy_init();
    struct curl_slist *header = curl_slist_append(NULL, auth_header);
    struct string *buf = malloc(sizeof(struct string));

    init_string(buf);

    curl_easy_setopt(handle, CURLOPT_URL, channel_info_url);
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    struct discord_request *req = malloc(sizeof(struct discord_request));
    req->handle = handle;
    req->cb = &process_channel_info;
    req->data = buf;

    run_request(req);

    next_message_check.tv_sec++;
}


int need_check_messages()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return now.tv_sec > next_message_check.tv_sec ||
           (now.tv_sec == next_message_check.tv_sec && now.tv_nsec > next_message_check.tv_nsec);
}

void discord_run()
{
    CURLMcode mc;

    mc = curl_multi_perform(multi_handle, &running_queries);
    if (mc == CURLM_OK)
    {
        mc = curl_multi_poll(multi_handle, NULL, 0, 1, &running_queries);
    }

    if (mc != CURLM_OK)
    {
        fprintf(stderr, "curl_multi failed: code %d\n", mc);
        return;
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
            printf("HTTP transfer completed with status %d\n", msg->data.result);
            break;
        }

        int found = 0;
        for (size_t i = 0; i < MAX_REQUESTS; i++)
        {
            if (all_requests[i] != NULL && msg->easy_handle == all_requests[i]->handle)
            {
                long response_code;
                curl_easy_getinfo(all_requests[i]->handle, CURLINFO_RESPONSE_CODE, &response_code);
                all_requests[i]->cb(response_code, all_requests[i]->data);
                running_queries--;

                found = 1;

                //curl_multi_remove_handle(multi_handle, all_requests[i]->handle);
                //curl_easy_cleanup(all_requests[i]->handle);

                all_requests[i] = NULL;
                break;
            }
        }

        if (found == 0)
        {
            printf("warn: data from unknown handler %X\n", msg->easy_handle);
        }
    }

    if (last_message_id != NULL && need_check_messages())
    {
        request_new_messages_after(last_message_id);
    }
}

int main()
{
    discord_init();
    request_last_message_id();

    while (1)
    {
        discord_run();
    }
}
