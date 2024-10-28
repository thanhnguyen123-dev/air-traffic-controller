#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "airport.h"

#define PORT_STRLEN 6
#define DEFAULT_PORTNUM 1024
#define MIN_PORTNUM 1024
#define MAX_PORTNUM 65535

/** Struct that contains information associated with each airport node. */
typedef struct airport_node_info {
  int id;    /* Airport identifier */
  int port;  /* Port num associated with this airport's listening socket */
  pid_t pid; /* PID of the child process for this airport. */
} node_info_t;

/** Struct that contains parameters for the controller node and ATC network as
 *  a whole. */
typedef struct controller_params_t {
  int listenfd;               /* file descriptor of the controller listening socket */
  int portnum;                /* port number used to connect to the controller */
  int num_airports;           /* number of airports to create */
  int *gate_counts;           /* array containing the number of gates in each airport */
  node_info_t *airport_nodes; /* array of info associated with each airport */
} controller_params_t;

controller_params_t ATC_INFO;

shared_queue_t controller_shared_queue;

/** @brief The main server loop of the controller.
 *
 *  @todo  Implement this function!
 */
void controller_server_loop(void) {
  init_shared_queue(&controller_shared_queue, 20);

  pthread_t tid[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_create(&tid[i], NULL, controller_thread_routine, &controller_shared_queue) != 0) {
      perror("pthread_create");
      exit(1);
    }
  }

  int connfd;
  struct sockaddr_storage clientaddr;
  socklen_t clientlen = sizeof(struct sockaddr_storage);

  while (1) {
    if ((connfd = accept(ATC_INFO.listenfd, (SA *)&clientaddr, &clientlen)) < 0) {
      perror("accept");
      continue;
    }
    add_client_connection(&controller_shared_queue, connfd);
  }

  deinit_shared_queue(&controller_shared_queue);
}

void *controller_thread_routine(void *arg) {
  pthread_detach(pthread_self());
  shared_queue_t *s_que = (shared_queue_t *)arg;
  int connfd, airport_id;
  char buf[MAXBUF];
  rio_t controller_rio, airport_rio;

  while (1) {
    connfd = get_client_connection(s_que);

    rio_readinitb(&controller_rio, connfd);
    ssize_t n;
    while ((n = rio_readlineb(&controller_rio, buf, MAXLINE)) > 0) {
      buf[n] = '\0';
      if (strcmp(buf, "\n") == 0) {
        break;
      }
      
      char command[20], response[MAXBUF], port_str[PORT_STRLEN];
      int args[5];
      int toks_cnt;
      toks_cnt = sscanf(buf, "%s %d %d %d %d %d", command, &args[0], &args[1], &args[2], &args[3], &args[4]);
      if (is_valid_schedule_request(command, toks_cnt) ||
          is_valid_plane_status_request(command, toks_cnt) ||
          is_valid_time_status_request(command, toks_cnt)) {
        airport_id = args[0];
      }
      else {
        sprintf(response, "Error: Invalid request provided\n");
        rio_writen(connfd, response, strlen(response));
        continue;
      }

      if (airport_id >= 0 && airport_id < ATC_INFO.num_airports) {
        snprintf(port_str, PORT_STRLEN, "%d", ATC_INFO.airport_nodes[airport_id].port);
        int airport_fd = open_clientfd("localhost", port_str);

        rio_readinitb(&airport_rio, airport_fd);

        rio_writen(airport_fd, buf, strlen(buf));
        rio_writen(airport_fd, "\n", 1);

        ssize_t n;
        while ((n = rio_readlineb(&airport_rio, response, MAXLINE)) > 0) {
          rio_writen(connfd, response, n);
        }

        close(airport_fd);
      }
      else {
        sprintf(response, "Error: Airport %d does not exist\n", airport_id);
        rio_writen(connfd, response, strlen(response));
      }
    }
    close(connfd);
  }
  return NULL;
}

/** @brief A handler for reaping child processes (individual airport nodes).
 *         It may be helpful to set a breakpoint here when trying to debug
 *         issues that cause your airport nodes to crash.
 */
void sigchld_handler(int sig) {
  while (waitpid(-1, 0, WNOHANG) > 0)
    ;
  return;
}

/** You should not modify any of the functions below this point, nor should you
 *  call these functions from anywhere else in your code. These functions are
 *  used to handle the initial setup of the Air Traffic Control system.
 */

/** @brief This function spawns child processes for each airport node, and
 *         opens a listening socket for the controller to u.
 */
void initialise_network(void) {
  char port_str[PORT_STRLEN];
  int num_airports = ATC_INFO.num_airports;
  int lfd, idx, port_num = ATC_INFO.portnum;
  node_info_t *node;
  pid_t pid;

  snprintf(port_str, PORT_STRLEN, "%d", port_num);
  if ((ATC_INFO.listenfd = open_listenfd(port_str)) < 0) {
    perror("[Controller] open_listenfd");
    exit(1);
  }

  for (idx = 0; idx < num_airports; idx++) {
    node = &ATC_INFO.airport_nodes[idx];
    node->id = idx;
    node->port = ++port_num;
    snprintf(port_str, PORT_STRLEN, "%d", port_num);
    if ((lfd = open_listenfd(port_str)) < 0) {
      perror("open_listenfd");
      continue;
    }
    if ((pid = fork()) == 0) {
      close(ATC_INFO.listenfd);
      initialise_node(idx, ATC_INFO.gate_counts[idx], lfd);
      exit(0);
    } else if (pid < 0) {
      perror("fork");
    } else {
      node->pid = pid;
      fprintf(stderr, "[Controller] Airport %d assigned port %s\n", idx, port_str);
      close(lfd);
    }
  }

  signal(SIGCHLD, sigchld_handler);
  controller_server_loop();
  exit(0);
}

/** @brief Prints usage information for the program and then exits. */
void print_usage(char *program_name) {
  printf("Usage: %s [-n N] [-p P] -- [gate count list]\n", program_name);
  printf("  -n: Number of airports to create.\n");
  printf("  -p: Port number to use for controller.\n");
  printf("  -h: Print this help message and exit.\n");
  exit(0);
}

/** @brief   Parses the gate counts provided for each airport given as the final
 *           argument to the program.
 *
 *  @param list_arg argument string containing the integer list
 *  @param expected expected number of integer values to read from the list.
 *
 *
 *  @returns An allocated array of gate counts for each airport, or `NULL` if
 *           there was an issue in parsing the gate counts.
 *
 *  @warning If a list of *more* than `expected` integers is given as an argument,
 *           then all integers after the nth are silently ignored.
 */
int *parse_gate_counts(char *list_arg, int expected) {
  int *arr, n = 0, idx = 0;
  char *end, *buff = list_arg;
  if (!list_arg) {
    fprintf(stderr, "Expected gate counts for %d airport nodes.\n", expected);
    return NULL;
  }
  end = list_arg + strlen(list_arg);
  arr = calloc(1, sizeof(int) * (unsigned)expected);
  if (arr == NULL)
    return NULL;

  while (buff < end && idx < expected) {
    if (sscanf(buff, "%d%n%*c%n", &arr[idx++], &n, &n) != 1) {
      break;
    } else {
      buff += n;
    }
  }

  if (idx < expected) {
    fprintf(stderr, "Expected %d gate counts, got %d instead.\n", expected, idx);
    free(arr);
    arr = NULL;
  }

  return arr;
}

/** @brief Parses and validates the arguments used to create the Air Traffic
 *         Control Network. If successful, the `ATC_INFO` variable will be
 *         initialised.
 */
int parse_args(int argc, char *argv[]) {
  int c, ret = 0, *gate_counts = NULL;
  int atc_portnum = DEFAULT_PORTNUM;
  int num_airports = 0;
  int max_portnum = MAX_PORTNUM;

  while ((c = getopt(argc, argv, "n:p:h")) != -1) {
    switch (c) {
    case 'n':
      sscanf(optarg, "%d", &num_airports);
      max_portnum -= num_airports;
      break;
    case 'p':
      sscanf(optarg, "%d", &atc_portnum);
      break;
    case 'h':
      print_usage(argv[0]);
      break;
    case '?':
      fprintf(stderr, "Unknown Option provided: %c\n", optopt);
      ret = -1;
    default:
      break;
    }
  }

  if (num_airports <= 0) {
    fprintf(stderr, "-n must be greater than 0.\n");
    ret = -1;
  }
  if (atc_portnum < MIN_PORTNUM || atc_portnum >= max_portnum) {
    fprintf(stderr, "-p must be between %d-%d.\n", MIN_PORTNUM, max_portnum);
    ret = -1;
  }

  if (ret >= 0) {
    if ((gate_counts = parse_gate_counts(argv[optind], num_airports)) == NULL)
      return -1;
    ATC_INFO.num_airports = num_airports;
    ATC_INFO.gate_counts = gate_counts;
    ATC_INFO.portnum = atc_portnum;
    ATC_INFO.airport_nodes = calloc((unsigned)num_airports, sizeof(node_info_t));
  }

  return ret;
}

int main(int argc, char *argv[]) {
  if (parse_args(argc, argv) < 0)
    return 1;
  initialise_network();
  controller_server_loop();
  return 0;
}
