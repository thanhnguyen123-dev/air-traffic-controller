#include "airport.h"

/** This is the main file in which you should implement the airport server code.
 *  There are many functions here which are pre-written for you. You should read
 *  the comments in the corresponding `airport.h` header file to understand what
 *  each function does, the arguments they accept and how they are intended to
 *  be used.
 *
 *  You are encouraged to implement your own helper functions to handle requests
 *  in airport nodes in this file. You are also permitted to modify the
 *  functions you have been given if needed.
 */

/* This will be set by the `initialise_node` function. */
static int AIRPORT_ID = -1;

/* This will be set by the `initialise_node` function. */
static airport_t *AIRPORT_DATA = NULL;

gate_t *get_gate_by_idx(int gate_idx) {
  if ((gate_idx) < 0 || (gate_idx > AIRPORT_DATA->num_gates))
    return NULL;
  else
    return &AIRPORT_DATA->gates[gate_idx];
}

time_slot_t *get_time_slot_by_idx(gate_t *gate, int slot_idx) {
  if ((slot_idx < 0) || (slot_idx >= NUM_TIME_SLOTS))
    return NULL;
  else
    return &gate->time_slots[slot_idx];
}

int check_time_slots_free(gate_t *gate, int start_idx, int end_idx) {
  time_slot_t *ts;
  int idx;
  for (idx = start_idx; idx <= end_idx; idx++) {
    ts = get_time_slot_by_idx(gate, idx);
    if (ts->status == 1)
      return 0;
  }
  return 1;
}

int set_time_slot(time_slot_t *ts, int plane_id, int start_idx, int end_idx) {
  if (ts->status == 1)
    return -1;
  ts->status = 1; /* Set to be occupied */
  ts->plane_id = plane_id;
  ts->start_time = start_idx;
  ts->end_time = end_idx;
  return 0;
}

int add_plane_to_slots(gate_t *gate, int plane_id, int start, int count) {
  int ret = 0, end = start + count;
  time_slot_t *ts = NULL;
  for (int idx = start; idx <= end; idx++) {
    ts = get_time_slot_by_idx(gate, idx);
    ret = set_time_slot(ts, plane_id, start, end);
    if (ret < 0) break;
  }
  return ret;
}

int search_gate(gate_t *gate, int plane_id) {
  int idx, next_idx;
  time_slot_t *ts = NULL;
  for (idx = 0; idx < NUM_TIME_SLOTS; idx = next_idx) {
    ts = get_time_slot_by_idx(gate, idx);
    if (ts->status == 0) {
      next_idx = idx + 1;
    } else if (ts->plane_id == plane_id) {
      return idx;
    } else {
      next_idx = ts->end_time + 1;
    }
  }
  return -1;
}

time_info_t lookup_plane_in_airport(int plane_id) {
  time_info_t result = {-1, -1, -1};
  int gate_idx, slot_idx;
  gate_t *gate;
  for (gate_idx = 0; gate_idx < AIRPORT_DATA->num_gates; gate_idx++) {
    gate = get_gate_by_idx(gate_idx);
    if ((slot_idx = search_gate(gate, plane_id)) >= 0) {
      result.start_time = slot_idx;
      result.gate_number = gate_idx;
      result.end_time = get_time_slot_by_idx(gate, slot_idx)->end_time;
      break;
    }
  }
  return result;
}

int assign_in_gate(gate_t *gate, int plane_id, int start, int duration, int fuel) {
  int idx, end = start + duration;
  for (idx = start; idx <= (start + fuel) && (end < NUM_TIME_SLOTS); idx++) {
    if (check_time_slots_free(gate, idx, end)) {
      add_plane_to_slots(gate, plane_id, idx, duration);
      return idx;
    }
    end++;
  }
  return -1;
}

time_info_t schedule_plane(int plane_id, int start, int duration, int fuel) {
  time_info_t result = {-1, -1, -1};
  gate_t *gate;
  int gate_idx, slot;
  for (gate_idx = 0; gate_idx < AIRPORT_DATA->num_gates; gate_idx++) {
    gate = get_gate_by_idx(gate_idx);
    if ((slot = assign_in_gate(gate, plane_id, start, duration, fuel)) >= 0) {
      result.start_time = slot;
      result.gate_number = gate_idx;
      result.end_time = slot + duration;
      break;
    }
  }
  return result;
}

airport_t *create_airport(int num_gates) {
  airport_t *data = NULL;
  size_t memsize = 0;
  if (num_gates > 0) {
    memsize = sizeof(airport_t) + (sizeof(gate_t) * (unsigned)num_gates);
    data = calloc(1, memsize);
  }
  if (data)
    data->num_gates = num_gates;
  return data;
}

void initialise_node(int airport_id, int num_gates, int listenfd) {
  AIRPORT_ID = airport_id;
  AIRPORT_DATA = create_airport(num_gates);
  if (AIRPORT_DATA == NULL)
    exit(1);
  airport_node_loop(listenfd);
}

void airport_node_loop(int listenfd) {
  /** TODO: implement the main server loop for an individual airport node here. */
  int connfd;
  char buf[MAXBUF];
  rio_t rio;
  struct sockaddr_storage clientaddr;
  socklen_t clientlen = sizeof(struct sockaddr_storage);

  while (1) {
    /* ... */
    if ((connfd = accept(listenfd, (SA *)&clientaddr, &clientlen)) < 0) {
      perror("accept");
      continue;
    }

    rio_readinitb(&rio, connfd);
    ssize_t n;
    while ((n = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
      buf[n] = '\0';
      if (strcmp(buf, "\n") == 0) {
        break;
      }
      process_request(buf, connfd);
    }

    close(connfd);

  }
}

void process_request(char *request_buf, int connfd) {
  char response[MAXBUF];
  char command[20];
  int args[5];
  int toks_cnt;
  toks_cnt = sscanf(request_buf, "%s %d %d %d %d %d", command, &args[0], &args[1], &args[2], &args[3], &args[4]);


  if (is_valid_schedule_request(command, toks_cnt)) {
    process_schedule(args, response);
  }

  else if (is_valid_plane_status_request(command, toks_cnt)) {
    process_plane_status(args, response);
  }

  else if (is_valid_time_status_request(command, toks_cnt)) {
    process_time_status(args, response);
  }

  else {
    snprintf(response, MAXLINE, "Error: Invalid request provided\n");
  }

  rio_writen(connfd, response, strlen(response));
}

void process_schedule(int *args, char *response) {
  int plane_id = args[1];
  int earliest_time = args[2]; 
  int duration = args[3];
  int fuel = args[4];

  if (earliest_time < 0 || earliest_time >= NUM_TIME_SLOTS) {
    snprintf(response, MAXLINE, "Error: Invalid 'earliest' time (%d)\n", earliest_time);
    return;
  }

  if (duration < 0 || duration >= NUM_TIME_SLOTS || earliest_time + duration >= NUM_TIME_SLOTS) {
    snprintf(response, MAXLINE, "Error: Invalid 'duration' value (%d)\n", duration);
    return;
  }

  time_info_t time_info = schedule_plane(plane_id, earliest_time, duration, fuel);

  if (time_info.start_time != -1) {
    snprintf(response, MAXLINE, "SCHEDULED %d at GATE %d: %02d:%02d-%02d:%02d\n", 
      plane_id, time_info.gate_number, 
      IDX_TO_HOUR(time_info.start_time), IDX_TO_MINS(time_info.start_time),
      IDX_TO_HOUR(time_info.end_time), IDX_TO_MINS(time_info.end_time));
  }
  else {
    snprintf(response, MAXLINE, "Error: Cannot schedule %d\n", plane_id);
  }
}

void process_plane_status(int *args, char *response) {
  int plane_id = args[1];

  time_info_t time_info = lookup_plane_in_airport(plane_id);

  if (time_info.start_time != -1) {
    snprintf(response, MAXLINE, "PLANE %d scheduled at GATE %d: %02d:%02d-%02d:%02d\n",
      plane_id, time_info.gate_number, 
      IDX_TO_HOUR(time_info.start_time), IDX_TO_MINS(time_info.start_time),
      IDX_TO_HOUR(time_info.end_time), IDX_TO_MINS(time_info.end_time));
  }
  else {
    snprintf(response, MAXLINE, "PLANE %d not scheduled at airport %d\n", plane_id, AIRPORT_ID);
  }
}

void process_time_status(int *args, char *response) {
  int gate_num = args[1];
  int start_idx = args[2];
  int duration = args[3];

  if (gate_num < 0 || gate_num >= AIRPORT_DATA->num_gates) {
    snprintf(response, MAXLINE, "Error: Invalid 'gate' value (%d)\n", gate_num);
    return;
  }

  if (duration < 0 || duration >= NUM_TIME_SLOTS || start_idx + duration >= NUM_TIME_SLOTS) {
    snprintf(response, MAXLINE, "Error: Invalid 'duration' value (%d)\n", duration);
    return;
  }


  gate_t *gate = get_gate_by_idx(gate_num);
  if (gate == NULL) {
    snprintf(response, MAXLINE, "Error: Invalid 'gate' value (%d)\n", gate_num);
    return;
  }

  char status_str[MAXBUF] = "";
  int end_idx = start_idx + duration;

  for (int i = start_idx; i <= end_idx; i++) {
    time_slot_t *slot = get_time_slot_by_idx(gate, i);
    if (slot == NULL) {
      snprintf(response, MAXLINE, "Error: Invalid request provided\n");
      return;
    }

    char status = (slot->status == 1) ? 'A' : 'F';
    int flight_id = (slot->status == 1) ? slot->plane_id : 0;

    char line[MAXLINE];
    snprintf(line, sizeof(line), "AIRPORT %d GATE %d %02d:%02d: %c - %d\n", 
      AIRPORT_ID, gate_num, IDX_TO_HOUR(i), IDX_TO_MINS(i), status, flight_id);

    strcat(status_str, line);
  }
  strcpy(response, status_str);
}

int is_valid_schedule_request(char *command, int toks_cnt) {
  return strcmp(command, "SCHEDULE") == 0 && toks_cnt == 6;
}

int is_valid_plane_status_request(char *command, int toks_cnt) {
  return strcmp(command, "PLANE_STATUS") == 0 && toks_cnt == 3;
}

int is_valid_time_status_request(char *command, int toks_cnt) {
  return strcmp(command, "TIME_STATUS") == 0 && toks_cnt == 5;
}

void init_shared_queue(shared_queue_t *s_que, int n) {
  s_que->n = n;
  s_que->front = s_que->rear = 0;
  s_que->fds_buf = calloc(n, sizeof(int));
  pthread_mutex_init(&s_que->lock, NULL);
  pthread_cond_init(&s_que->slots, NULL);
  pthread_cond_init(&s_que->items, NULL);
}

void add_client_connection(shared_queue_t *s_que, int connfd) {
  pthread_mutex_lock(&s_que->lock);
  while (s_que->count == s_que->n) {
    pthread_cond_wait(&s_que->slots, &s_que->lock);
  }

  // add connfd to the buffer
  s_que->fds_buf[(++s_que->rear) % s_que->n] = connfd;
  s_que->count++;

  pthread_cond_signal(&s_que->items);
  pthread_mutex_unlock(&s_que->lock);
}

int get_client_connection(shared_queue_t *s_que) {
  int connfd;
  pthread_mutex_lock(&s_que->lock);
  while (s_que->count == 0) {
    pthread_cond_wait(&s_que->items, &s_que->lock);
  }

  // remove connfd from the buffer
  connfd = s_que->fds_buf[(s_que->front++) % s_que->n];
  s_que->count--;

  pthread_cond_signal(&s_que->slots);
  pthread_mutex_unlock(&s_que->lock);
  return connfd;
}


