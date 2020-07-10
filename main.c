#include <curl/curl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "auth.h"

#define APPLICATION_INFO_URL "https://discord.com/api/oauth2/applications/@me"
#define GET_CHANNELS_URL "https://discord.com/api/channels"

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

void init_string(struct string *s)
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

int main()
{
    CURL *curl;
    CURLM *multi_handle;
    CURLcode res;
    int still_running = 0;

    struct string s;

    init_string(&s);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, BOT_AUTHORIZATION_HEADER);

    char messages_url[100];
    sprintf(messages_url, "https://discord.com/api/channels/%s/messages\0", CHANNEL_ID);

    curl_easy_setopt(curl, CURLOPT_URL, messages_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

    multi_handle = curl_multi_init();
    curl_multi_add_handle(multi_handle, curl);
    curl_multi_perform(multi_handle, &still_running);

    do
    {
        CURLMcode mc;
        int numfds;

        mc = curl_multi_perform(multi_handle, &still_running);

        if (mc == CURLM_OK)
        {
            /* wait for activity or timeout */
            mc = curl_multi_poll(multi_handle, NULL, 0, 0, &numfds);
        }

        if (mc != CURLM_OK)
        {
            fprintf(stderr, "curl_multi failed, code %d.\n", mc);
            break;
        }

        printf("steel running\n");

    } while (still_running);

    /* See how the transfers went */
    CURLMsg *msg;  /* for picking up messages with the transfer status */
    int msgs_left; /* how many messages are left */
    while ((msg = curl_multi_info_read(multi_handle, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE)
        {
            if (msg->data.result != CURLE_OK) {
                printf("HTTP transfer completed with status %d", msg->data.result);
                break;
            }

            size_t msg_len;
            struct message *messages;

            messages = get_messags_reponse_to_list(s.ptr, &msg_len);
            for (int i = 0; i < msg_len; i++) {
                struct message msg = messages[i];
                printf("%s: %s\n", msg.from, msg.content);
            }

        }
    }

    return 0;
}