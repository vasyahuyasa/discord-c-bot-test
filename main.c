#include <curl/curl.h>
#include <unistd.h>
#include <stdlib.h> 
#include <string.h>

struct string
{
    char *ptr;
    size_t len;
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

int main()
{
    CURL *curl;
    CURLM *multi_handle;
    CURLcode res;
    int still_running = 0;
    CURLMsg *msg;  /* for picking up messages with the transfer status */
    int msgs_left; /* how many messages are left */
    struct string s;

    init_string(&s);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();

    curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

    multi_handle = curl_multi_init();
    curl_multi_add_handle(multi_handle, curl);
    curl_multi_perform(multi_handle, &still_running);

    while (still_running)
    {
        struct timeval timeout = {0, 0};
        int rc;       /* select() return code */
        CURLMcode mc; /* curl_multi_fdset() return code */

        fd_set fdread;
        fd_set fdwrite;
        fd_set fdexcep;
        int maxfd = -1;

        long curl_timeo = -1;

        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);

        printf("still running\n");

        mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

        if (mc != CURLM_OK)
        {
            fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
            break;
        }

        if (maxfd != -1)
        {
            rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
        }

        switch (rc)
        {
        case -1:
            /* select error */
            break;
        case 0:  /* timeout */
        default: /* action */
            curl_multi_perform(multi_handle, &still_running);
            printf("still running\n");
            usleep(1000 * 10);
            break;
        }
    }

    /* See how the transfers went */
    while ((msg = curl_multi_info_read(multi_handle, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE)
        {
            printf("HTTP transfer completed with status %d\n%s", msg->data.whatever, s.ptr);
        }
    }

    curl_multi_cleanup(multi_handle);
    curl_easy_cleanup(curl);

    return 0;
}