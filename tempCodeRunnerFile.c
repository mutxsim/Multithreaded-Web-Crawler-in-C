#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <curl/curl.h>         // For fetching HTML content
#include <libxml/HTMLparser.h> // For HTML parsing

#define MAX_URL_LEN 1024
#define MAX_DEPTH 5 // Maximum depth to crawl

// Structure for queue elements.
typedef struct URLQueueNode
{
    char url[MAX_URL_LEN];
    int depth;
    struct URLQueueNode *next;
} URLQueueNode;

// Structure for a thread-safe queue.
typedef struct
{
    URLQueueNode *head, *tail;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int count;
} URLQueue;

// Initialize a URL queue.
void initQueue(URLQueue *queue)
{
    queue->head = queue->tail = NULL;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->cond, NULL);
    queue->count = 0;
}

// Add a URL to the queue.
void enqueue(URLQueue *queue, const char *url, int depth)
{
    URLQueueNode *newNode = malloc(sizeof(URLQueueNode));
    if (!newNode)
    {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return;
    }
    strncpy(newNode->url, url, MAX_URL_LEN - 1);
    newNode->url[MAX_URL_LEN - 1] = '\0';
    newNode->depth = depth;
    newNode->next = NULL;

    pthread_mutex_lock(&queue->lock);
    if (queue->tail)
    {
        queue->tail->next = newNode;
    }
    else
    {
        queue->head = newNode;
    }
    queue->tail = newNode;
    queue->count++;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->lock);
}

// Remove a URL from the queue.
URLQueueNode *dequeue(URLQueue *queue)
{
    pthread_mutex_lock(&queue->lock);
    while (queue->count == 0)
    {
        pthread_cond_wait(&queue->cond, &queue->lock);
    }

    URLQueueNode *temp = queue->head;
    if (!temp)
    {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }

    queue->head = queue->head->next;
    if (!queue->head)
    {
        queue->tail = NULL;
    }
    queue->count--;
    pthread_mutex_unlock(&queue->lock);
    return temp;
}

// Function to fetch HTML content from a URL using libcurl.
char *fetch_html_content(const char *url)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        fprintf(stderr, "Error: Unable to initialize libcurl\n");
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL); // To receive data

    // Buffer to store HTML content
    char *html_content = malloc(1);
    if (!html_content)
    {
        fprintf(stderr, "Error: Memory allocation failed\n");
        curl_easy_cleanup(curl);
        return NULL;
    }
    html_content[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html_content);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        fprintf(stderr, "Error: Failed to fetch URL: %s\n", url);
        free(html_content);
        html_content = NULL;
    }

    curl_easy_cleanup(curl);
    return html_content;
}

// Function to parse HTML content and extract links using libxml.
void parse_html_links(const char *html_content, URLQueue *queue, int depth)
{
    htmlParserCtxtPtr ctxt = htmlCreateMemoryParserCtxt(html_content, strlen(html_content));
    if (!ctxt)
    {
        fprintf(stderr, "Error: Failed to create HTML parser context\n");
        return;
    }

    htmlDocPtr doc = htmlCtxtReadDoc(ctxt, NULL, NULL, NULL, HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    if (!doc)
    {
        fprintf(stderr, "Error: Failed to parse HTML content\n");
        htmlFreeParserCtxt(ctxt);
        return;
    }

    htmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root)
    {
        fprintf(stderr, "Error: Empty HTML document\n");
        xmlFreeDoc(doc);
        htmlFreeParserCtxt(ctxt);
        return;
    }

    // Iterate through the HTML tree to find anchor tags (links)
    for (htmlNodePtr node = root; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE && strcasecmp((const char *)node->name, "a") == 0)
        {
            xmlChar *href = xmlGetProp(node, (const xmlChar *)"href");
            if (href)
            {
                // Add extracted URL to the queue
                enqueue(queue, (const char *)href, depth + 1);
                xmlFree(href);
            }
        }
    }

    xmlFreeDoc(doc); // Corrected function call
    htmlFreeParserCtxt(ctxt);
}

// Placeholder for the function to process a URL.
void process_url(const char *url)
{
    // Placeholder for URL processing logic
    printf("Processing URL: %s\n", url);
}

// Main function to drive the web crawler.
void *fetch_url(void *arg)
{
    URLQueue *queue = (URLQueue *)arg;

    while (1)
    {
        URLQueueNode *node = dequeue(queue);
        if (!node)
            break;

        char *html_content = fetch_html_content(node->url);
        if (html_content)
        {
            process_url(node->url);
            if (node->depth < MAX_DEPTH)
            {
                parse_html_links(html_content, queue, node->depth);
            }
            free(html_content);
        }
        free(node);
    }

    return NULL;
}

// Main function to drive the web crawler.
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <starting-url>\n", argv[0]);
        return 1;
    }

    URLQueue queue;
    initQueue(&queue);
    enqueue(&queue, argv[1], 0); // Starting URL with depth 0

    const int NUM_THREADS = 4; // Number of threads
    pthread_t threads[NUM_THREADS];

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_ALL);

    // Create worker threads
    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_create(&threads[i], NULL, fetch_url, (void *)&queue);
    }

    // Join threads after completion.
    for (int i = 0; i < NUM_THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }

    // Cleanup libcurl
    curl_global_cleanup();

    return 0;
}
