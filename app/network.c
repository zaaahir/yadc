#include "network.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct response_content {
  char *content;
  size_t size;
};
/**
 * Do a web request
 * @param uri The URL to call
 * @param bearer_token (optional) the auth token to be included in the request
 * header
 * @return the response content
 */
char *get_response(char *uri, char *bearer_token) {
  CURL *curl_handle;
  char *content = NULL;
  char url2[200];
  strcpy(url2, uri);
  // init our response content structure
  struct response_content response;
  response.content = malloc(1);
  response.size = 0;
  curl_handle = curl_easy_init();
  if (curl_handle) {
    // printf("curl: url: %s\n", url2);
    curl_easy_setopt(curl_handle, CURLOPT_URL, (void *)url2);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_handler_mem);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&response);
    curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0);
    // set the headers
    struct curl_slist *list = NULL;
    if (bearer_token != NULL) {
      list = curl_slist_append(list, bearer_token);
      curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, list);
    }
    // printf("curl: performing operation\n");
    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
      fprintf(stderr, "Error from curl: %i %s\n", res, curl_easy_strerror(res));
    } else {
      content = malloc(response.size + 1);
      strcpy(content, response.content);
    }
    free(response.content);
    if (list != NULL) {
      curl_slist_free_all(list);
    }
    curl_easy_cleanup(curl_handle);
  }
  return content;
}
/**
 * Download a file to the disk
 * @param uri the URL to call
 * @param file the location where to save the downloaded content to
 * @param bearer_token (optional) the auth token to be included in the request
 * header
 * @return -1 on error, 0 on success
 */
int download_file(char *uri, char *file, char *bearer_token) {
  int result = -1;
  FILE *fptr;
  CURL *curl_handle = curl_easy_init();
  char url2[200];
  strcpy(url2, uri);
  if (curl_handle) {
    if ((fptr = fopen(file, "w")) == NULL) {
      perror("Error: Could open destination for writing");
      result = -1;
    } else {
      // printf("curl: url: %s\n", url2);
      curl_easy_setopt(curl_handle, CURLOPT_URL, (void *)url2);
      curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
      // set the headers
      struct curl_slist *list = NULL;
      if (bearer_token != NULL) {
        list = curl_slist_append(list, bearer_token);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, list);
      }
      curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_handler_disk);
      curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)fptr);
      curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0);
      // printf("curl: performing operation\n");
      CURLcode res = curl_easy_perform(curl_handle);
      if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %i %s\n", res,
                curl_easy_strerror(res));
      }
      if (list != NULL) {
        curl_slist_free_all(list);
      }
      fflush(fptr);
      fclose(fptr);
    }
    curl_easy_cleanup(curl_handle);
    result = 0;
  } else {
    printf("Could not download file\n");
    result = -1;
  }
  return result;
}
/**
 * Custom libcurl write handler to save response content to memory buffer
 */
size_t write_handler_mem(void *data, size_t size, size_t nmemb, void *arg) {
  size_t chunk_size = size * nmemb;
  struct response_content *res = (struct response_content *)arg;
  char *ptr = realloc(res->content, res->size + chunk_size + 1);
  if (ptr == NULL) {
    perror("Error: Out of memory\n");
    return 0;
  }
  res->content = ptr;
  memcpy(&(res->content[res->size]), data, chunk_size);
  res->size += chunk_size;
  res->content[res->size] = 0;
  return chunk_size;
}
/**
 * Custom libcurl write handler to savbe response content to disk file
 */
size_t write_handler_disk(void *contents, size_t size, size_t nitems,
                          FILE *file) {
  return fwrite(contents, size, nitems, file);
}
