#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#define URLLEN 1000

pthread_mutex_t lock;
int max_depth; // Maximum depth to crawl

int max_connections = 200;
int max_total = 100;
int max_requests = 500;
int max_link_per_page = 5;
int followlinks = 0;
int pending_interrupt = 0;

void sighandler(int dummy)
{
    pending_interrupt = 1;
}

typedef struct
{
    char *buf;
    size_t size;
} memory;

// Function to handle errors
void handle_error(const char *msg)
{
    fprintf(stderr, "Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

size_t grow_buffer(void *contents, size_t sz, size_t nmemb, void *ctx)
{
    size_t realsize = sz * nmemb;
    memory *mem = (memory *)ctx;
    char *ptr = realloc(mem->buf, mem->size + realsize);
    if (!ptr)
    {
        handle_error("not enough memory (realloc returned NULL)");
    }
    mem->buf = ptr;
    memcpy(&(mem->buf[mem->size]), contents, realsize);
    mem->size += realsize;
    return realsize;
}

CURL *make_handle(char *url)
{
    CURL *handle = curl_easy_init();
    if (!handle)
    {
        handle_error("failed to initialize libcurl");
    }
    curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(handle, CURLOPT_URL, url);
    memory *mem = malloc(sizeof(memory));
    if (!mem)
    {
        handle_error("failed to allocate memory for memory buffer");
    }
    mem->size = 0;
    mem->buf = malloc(1);
    if (!mem->buf)
    {
        handle_error("failed to allocate memory for buffer data");
    }
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, grow_buffer);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, mem);
    curl_easy_setopt(handle, CURLOPT_PRIVATE, mem);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(handle, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(handle, CURLOPT_FILETIME, 1L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "Crawler Project");
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    curl_easy_setopt(handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    curl_easy_setopt(handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);
    return handle;
}

size_t follow_links(CURLM *multi_handle, memory *mem, char *url, int depth)
{
    if (depth >= max_depth)
        return 0; // If depth exceeds the maximum, return
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR |
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(mem->buf, mem->size, url, NULL, opts);
    if (!doc)
        return 0;

    xmlChar *xpath = (xmlChar *)"//a/@href";
    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    xmlXPathObjectPtr result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (!result)
        return 0;

    xmlNodeSetPtr nodeset = result->nodesetval;
    if (xmlXPathNodeSetIsEmpty(nodeset))
    {
        xmlXPathFreeObject(result);
        return 0;
    }
    size_t count = 0;
    int i;
    for (i = 0; i < nodeset->nodeNr; i++)
    {
        double r = rand();
        int x = r * nodeset->nodeNr / RAND_MAX;
        const xmlNode *node = nodeset->nodeTab[x]->xmlChildrenNode;
        xmlChar *href = xmlNodeListGetString(doc, node, 1);
        if (followlinks)
        {
            xmlChar *orig = href;
            href = xmlBuildURI(href, (xmlChar *)url);
            xmlFree(orig);
        }
        char *link = (char *)href;
        if (!link || strlen(link) < 20)
            continue;
        if (!strncmp(link, "http://", 7) || !strncmp(link, "https://", 8))
        {
            if (depth < max_depth - 1) // Check if we're within the depth limit
            {
                curl_multi_add_handle(multi_handle, make_handle(link));
                if (count++ == max_link_per_page)
                    break;
                count += follow_links(multi_handle, mem, link, depth + 1); // Recursive call with increased depth
            }
        }
        xmlFree(link);
    }
    xmlXPathFreeObject(result);
    return count;
}

int html_checker(char *ctype)
{
    return ctype != NULL && strlen(ctype) > 10 && strstr(ctype, "text/html");
}

void *crawler(void *url_ptr)
{
    char *url = (char *)url_ptr;
    pthread_mutex_lock(&lock);
    FILE *datafile = fopen("datafile.txt", "w"); // Open in write mode instead of append mode
    if (!datafile)
    {
        handle_error("failed to open datafile.txt for writing");
    }
    signal(SIGINT, sighandler);
    LIBXML_TEST_VERSION;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURLM *multi_handle = curl_multi_init();
    if (!multi_handle)
    {
        handle_error("failed to initialize libcurl multi handle");
    }
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, max_connections);
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, 6L);

#ifdef CURLPIPE_MULTIPLEX
    curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif

    curl_multi_add_handle(multi_handle, make_handle(url));

    int msgs_left;
    int pending = 0;
    int complete = 0;
    int still_running = 1;
    while (still_running && !pending_interrupt)
    {
        int numfds;
        curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
        curl_multi_perform(multi_handle, &still_running);

        CURLMsg *m = NULL;
        while ((m = curl_multi_info_read(multi_handle, &msgs_left)))
        {
            if (m->msg == CURLMSG_DONE)
            {
                CURL *handle = m->easy_handle;
                char *url;
                memory *mem;
                curl_easy_getinfo(handle, CURLINFO_PRIVATE, &mem);
                curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &url);
                if (m->data.result == CURLE_OK)
                {
                    long res_status;
                    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &res_status);
                    if (res_status == 200)
                    {
                        char *ctype;
                        curl_easy_getinfo(handle, CURLINFO_CONTENT_TYPE, &ctype);
                        fprintf(datafile, "[%d]: %s\n", complete, url);
                        if (html_checker(ctype) && mem->size > 100)
                        {
                            if (pending < max_requests && (complete + pending) < max_total)
                            {
                                pending += follow_links(multi_handle, mem, url, 0); // Start with depth 0 for the provided URL
                                still_running = 1;
                            }
                        }
                    }
                    else
                    {
                        fprintf(datafile, "[%d]: %s\n", complete, url);
                    }
                }
                else
                {
                    fprintf(stderr, "[%d] Connection failure: %s\n", complete, url);
                }
                curl_multi_remove_handle(multi_handle, handle);
                curl_easy_cleanup(handle);
                free(mem->buf);
                free(mem);
                complete++;
                pending--;
            }
        }
    }
    fclose(datafile);
    curl_multi_cleanup(multi_handle);
    curl_global_cleanup();
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <URL> <max_depth>\n", argv[0]);
        return EXIT_FAILURE;
    }

    pthread_mutex_init(&lock, NULL);
    char *url = argv[1];
    max_depth = atoi(argv[2]);

    pthread_t tid;
    int error = pthread_create(&tid, NULL, crawler, (void *)url);
    if (0 != error)
    {
        fprintf(stderr, "Couldn't create crawler thread, error: %d\n", error);
        return EXIT_FAILURE;
    }
    else
    {
        fprintf(stderr, "Crawler thread created for URL: %s\n", url);
    }

    pthread_join(tid, NULL);
    pthread_mutex_destroy(&lock);

    // Display the contents of the datafile
    FILE *datafile = fopen("datafile.txt", "r");
    if (!datafile)
    {
        handle_error("failed to open datafile.txt for reading");
    }

    char line[1000];
    while (fgets(line, sizeof(line), datafile))
    {
        printf("%s", line);
    }

    fclose(datafile);
    return EXIT_SUCCESS;
}
