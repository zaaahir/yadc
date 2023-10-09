#define _GNU_SOURCE
#include "docker-registry.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <netdb.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define PATH_SIZE 1024
/**
 * Initialize our docker image
 * @param image the user supplied image:tag of the docker image to download
 * @param dir the directory to extract the docker layers into
 * @return -1 on error, 0 on success
 */
int init_docker_image(char *image, char *dir) {
  // 0. first, extract the image and tag components
  size_t size = 0;
  char *tag = NULL;
  char *pstart = strstr(image, ":");
  // was a tag specified?
  if (pstart != NULL) {
    size = strlen(image) - strlen(pstart);
    tag = malloc(size + 2);
    strncpy(tag, pstart + 1, size);
    char *temp = malloc(strlen(pstart) + 1);
    strncpy(temp, image, strlen(pstart) - 1);
    image = temp;
  } else {
    tag = malloc(strlen("latest"));
    strcpy(tag, "latest");
  }
  // 1. get the auth token
  size = strlen("repository:library/") + strlen(image) + strlen(":") +
         strlen(tag) + strlen(",pull");
  char *scope = malloc(size + 1);
  strcpy(scope, "repository:library/");
  strcat(scope, image);
  strcat(scope, ":");
  strcat(scope, tag);
  strcat(scope, ",pull");
  char *token = NULL;
  if ((token = docker_registry_auth(scope)) == NULL) {
    free(scope);
    return -1;
  }
  // 2. pull the image manifest and extract the layers
  char **layer_ids;
  if ((layer_ids = docker_enumerate_layers(token, "library", image, tag)) ==
      NULL) {
    if (tag != NULL) {
      free(tag);
    }
    free(token);
    return -1;
  }
  // and finally download the layer blobs
  int result = 0;
  int index = 0;
  while (1) {
    char *id = layer_ids[index];
    if (id == NULL) {
      break;
    }
    // printf("--> pulling %s\n", id);
    if (docker_get_layer(token, dir, "library", image, id) != 0) {
      result = -1;
    }
    // free our id memory
    free(layer_ids[index++]);
  }
  if (tag != NULL) {
    free(tag);
  }
  free(layer_ids);
  free(scope);
  free(token);
  return result;
1
}
int copy_executed_file(char *source, char *destination) {
  FILE *filesource, *filedestination;
  char buffer[BUFSIZ];
  int s;
  // Try to read from source file
  if ((filesource = fopen(source, "rb")) == NULL) {
    perror("Error, could not open source");
    return -1;
  }
  // Try to write to destination file
  int fd = open(destination, O_RDWR | O_CREAT, 0777);
  if ((filedestination = fdopen(fd, "wb")) == NULL) {
    perror("Error, could not open destination");
    fclose(filesource);
    return -1;
  }
  // Copy contents of source file to destination
  while (!feof(filesource) && !ferror(filesource)) {
    s = fread(buffer, 1, BUFSIZ, filesource);
    if (s > 0) {
      s = fwrite(buffer, 1, s, filedestination);
    }
  }
  fclose(filedestination);
  fclose(filesource);
  return 0;
}
static void printlist(const char *arg) {
  DIR *dirp;
  struct dirent *dp;
  if ((dirp = opendir(arg)) == NULL) {
    perror("couldn't open");
    return;
  }
  printf("directory %s:\n", arg);
  do {
    errno = 0;
    if ((dp = readdir(dirp)) != NULL) {
      (void)printf("%s\n", dp->d_name);
    }
  } while (dp != NULL);
  closedir(dirp);
}
1
int setup_environment(char *command, char *image) {
  // Create the directory
  char template[] = "/tmp/some_dir.XXXXXX";
  char *temp_dir = mkdtemp(template);

  // initialize our image
  if (init_docker_image(image, temp_dir) == -1) {
    fprintf(stderr, "Error: Unable to download docker image %s\n", image);
    return -1;
  }
  // printlist(temp_dir);
  // Construct the destination path
  size_t size = strlen(temp_dir) + strlen(image);
  char *full_path_size = malloc(size + 1); // Add 1 for \0 character
  strcpy(full_path_size, temp_dir);
  strcat(full_path_size, "/");
  strcat(full_path_size, command);
  // Copy the file to the destination
  // printf("full path: %s\n", full_path_size);
  if (copy_executed_file(full_path_size, command) != 0) {
    printf("copy failed\n");
    free(full_path_size);
    return -1;
  }
  free(full_path_size);
  // chroot to activate our new environment
  if (chdir(temp_dir) || chroot(temp_dir)) {
    perror("Error, could not chroot to temporary directory");
    return -1;
  }
  return 0;
}
// Usage: your_docker.sh run <image> <command> <arg1> <arg2> ...
int main(int argc, char *argv[]) {
  // Disable output buffering
  setbuf(stdout, NULL);
  // You can use print statements as follows for debugging, they'll be visible
  // when running tests.
  // printf("Logs from your program will appear here!\n");
#define BUFFER_SIZE 1024
  int fd1[2];
  int fd2[2];
  int status2;
  char buffer[BUFFER_SIZE];
  pipe(fd1);
  pipe(fd2);
  char *command = argv[3];

  // Create a buffer for the docker image
  char docker_image[PATH_SIZE];
  strcpy(docker_image, argv[2]);
  // Setup the environment
  setup_environment(command, docker_image);
  int child_pid = fork();
  if (child_pid == -1) {
    // printf("Error forking!");
    return 1;
  }

  unshare(CLONE_NEWPID);
  if (child_pid == 0) {
    dup2(fd1[1], STDOUT_FILENO);
    dup2(fd2[1], STDERR_FILENO);
    close(fd1[0]);
    close(fd2[0]);
    close(fd1[2]);
    close(fd2[2]);

    if (execv(command, &argv[3]) == -1) {
      // printf("Error loading child process image %s: %s\n", command,
      // strerror(errno));
      return 1;
    }
  } else {
    close(fd1[1]);
    close(fd2[1]);
    // We're in parent
    waitpid(child_pid, &status2, 0);

    // printf("Child terminated, code %i\n", status2);
  }
  int ret;
  while ((ret = read(fd1[0], buffer, BUFFER_SIZE)) > 0) {
    write(STDOUT_FILENO, buffer, ret);
  }
  while ((ret = read(fd2[0], buffer, BUFFER_SIZE)) > 0) {
    write(STDERR_FILENO, buffer, ret);
  }
  return WEXITSTATUS(status2);
1
}
