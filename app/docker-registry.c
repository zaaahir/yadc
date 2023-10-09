#include "docker-registry.h"
#include "network.h"
#include "util.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
char *make_file_from_id(char *id);
char **parse_layers(char *response_content);
char *parse_token(char *response_content);
#define DOCKER_REGISTRY_AUTH_URI                                               \
  "https://auth.docker.io/token?service=registry.docker.io"
#define DOCKER_REGISTRY_IMAGES_URI "https://registry.hub.docker.com/v2"
/**
 * Request a bearer token from the docker authorization service
 * @param scope The scope the token is requested for
 * @param token If successfull, the token paremeter will be populated with the
 * issued bearer token
 * @return -1 on error, 0 on success
 */
char *docker_registry_auth(char *scope) {
  // construct the auth uri
  size_t size = strlen(DOCKER_REGISTRY_AUTH_URI) + strlen(scope);
  char *full_uri = malloc(size + 1);
  strcpy(full_uri, DOCKER_REGISTRY_AUTH_URI);
  strcat(full_uri, "&scope=");
  strcat(full_uri, scope);
  char *content, *token;
  if ((content = get_response(full_uri, NULL)) != NULL) {
    token = parse_token(content);
    free(content);
  }
  // puts(full_uri);
  free(full_uri);
  return token;
}
/**
 * Enumerate all layer id's in a image manifest
 * @param token the auth token
 * @param repo the repository
 * @param image the image
 * @param tag the tag
 * @return an array of id strings
 */
char **docker_enumerate_layers(char *token, char *repo, char *image,
                               char *tag) {
  char **layer_ids = NULL;
  size_t size = strlen(DOCKER_REGISTRY_IMAGES_URI) + strlen(repo) +
                strlen(image) + strlen(tag) + strlen("////") +
                strlen("manifests");
  char *full_uri = malloc(size + 1);
  strcpy(full_uri, DOCKER_REGISTRY_IMAGES_URI);
  strcat(full_uri, "/");
  strcat(full_uri, repo);
  strcat(full_uri, "/");
  strcat(full_uri, image);
  strcat(full_uri, "/");
  strcat(full_uri, "manifests");
  strcat(full_uri, "/");
  strcat(full_uri, tag);
  char *bearer_token = NULL;
  if (token != NULL) {
    size = strlen("Authorization: Bearer ") + strlen(token);
    bearer_token = malloc(size + 1);
    strcpy(bearer_token, "Authorization: Bearer ");
    strcat(bearer_token, token);
  }
  char *content;
  if ((content = get_response(full_uri, bearer_token)) != NULL) {
    layer_ids = parse_layers(content);
    free(content);
  }
  if (bearer_token != NULL) {
    free(bearer_token);
  }
  // puts(full_uri);
  return layer_ids;
}
/**
 * Download a layer  by id and untar it;s contents
 * @param token the auth token
 * @param dir the location where to write the layer blob to
 * @param repo the repository
 * @param image the image
 * @param id the layer identifier
 * @return -0 on success, -1 on error
 */
int docker_get_layer(char *token, char *dir, char *repo, char *image,
                     char *id) {
  size_t size = strlen(DOCKER_REGISTRY_IMAGES_URI) + strlen(repo) +
                strlen(image) + strlen("blobs") + strlen(id) + strlen("////");
  char *full_uri = malloc(size + 1);
  strcpy(full_uri, DOCKER_REGISTRY_IMAGES_URI);
  strcat(full_uri, "/");
  strcat(full_uri, repo);
  strcat(full_uri, "/");
  strcat(full_uri, image);
  strcat(full_uri, "/");
  strcat(full_uri, "blobs");
  strcat(full_uri, "/");
  strcat(full_uri, id);
  char *file_name = make_file_from_id(id);
  size = strlen(dir) + strlen(file_name);
  char *file = malloc(size + 2);
  strcpy(file, dir);
  strcat(file, "/");
  strcat(file, file_name);
  size = strlen("Authorization: Bearer ") + strlen(token);
  char *bearer_token = malloc(size + 1);
  strcpy(bearer_token, "Authorization: Bearer ");
  strcat(bearer_token, token);
  // printf("full uri: %s\n", full_uri);
  // printf("filename: %s\n", file);
  if (download_file(full_uri, file, bearer_token) == -1) {
    printf("Error downloading layer!");
  } else {
    // untar the file and delete the original
    untar(file, 1, 1);
  }
  free(file_name);
  free(full_uri);
  return 0;
}
/**
 * Helper function to remmove illegal characters from id to make it suitable as
 * a filename
 * @param id the layer identifier
 * @return the safe string
 */
char *make_file_from_id(char *id) {
  // layer id's look like this:
  // sha256:a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4
  // our file name will be everything after the :
  char *file = malloc(strlen(id));
  char *p = strstr(id, ":");
  strcpy(file, p + 1);
  return file;
}
/**
 * Helper function to add a string to a string array
 * @param array the curent array
 * @param size the size of the current array
 * @param string the string to add
 * @return the updated array
 */
char **add_string_to_array(char **array, int *size, const char *string) {
  char **new_arr = realloc(array, (*size + 1) * sizeof(char *));
  new_arr[*size] = malloc(strlen(string) + 1);
  strcpy(new_arr[*size], string);
  *size += 1;
  return new_arr;
}
/**
 * Helper function to parse response content and extract layer id's
 * @param response_content the content to parse
 * @return an array of string id's
 */
char **parse_layers(char *response_content) {
  if (response_content == NULL) {
    return NULL;
  }
  char **list = NULL;
  int list_size = 0;
  while (1) {
    // find the open segment
    char *pstart = strstr(response_content, "blobSum");
    if (pstart == NULL) {
      break;
    }
    // find the closing tag
    char *pend = strstr(pstart + 11, "\"");
    if (pend == NULL) {
      break;
    }
    int size = pend - pstart + 1;
    char *id = malloc(size + 1);
    memset(id, 0, size + 1);
    strncpy(id, pstart + 11, size - 12);
    list = add_string_to_array(list, &list_size, id);
    // move the response content pointer on
    response_content = pend;
  }
  // indicate the end of our list with a NULL
  list = add_string_to_array(list, &list_size, "end");
  list[list_size - 1] = NULL;
  return list;
}
/**
 * Helper function to parse response content for auth token
 * @param response_content the content to parse
 * @return the extracted token
 */
char *parse_token(char *response_content) {
  if (response_content == NULL) {
    return NULL;
  }
  // find the token segment
  char *pstart = strstr(response_content, "token");
  if (pstart == NULL) {
    return NULL;
  }
  // format is "token":"..... token data, move the ptr forward 8 chars
  pstart += 8;
  // locate the closing quotes
  char *pend = strstr(pstart, "\"");
  if (pend == NULL) {
    return NULL;
  }
  int size = pend - pstart;
  char *token = malloc(size);
  strncpy(token, pstart, size);
  return token;
}
