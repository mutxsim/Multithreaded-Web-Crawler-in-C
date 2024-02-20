# Multithreaded-Web-Crawler-in-C
#
# Description: a multithreaded web crawler in C that can efficiently fetchs,
# parses, and processes web pages from the internet. The goal is to utilize multiple threads to
# improve the performance and efficiency of the web crawling process.
#
# Functionality Description
# Web crawler includes the following functionalities:
#  • Multithreading: Use multiple threads to fetch web pages concurrently.
#  • URL Queue: Implement a thread-safe queue to manage URLs that are pending to be fetched.
#  • HTML Parsing: Extract links from the fetched web pages to find new URLs to crawl.
#  • Depth Control: Allow the crawler to limit the depth of the crawl to prevent infinite recursion.
#  • Synchronization: Implement synchronization mechanisms to manage access to shared resources among threads.
#  • Error Handling: Handle possible errors gracefully, including network errors, parsing errors, and dead links.
#  • Logging: Log the crawler’s activity, including fetched URLs and encountered errors.
