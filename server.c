/*** Includes ***/
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.c"
#define global static
#define local_persist static

typedef struct SwapList {
  size_t size;
  Swap *contents;
} SwapList;

double TmDateDistance(const struct tm *tm_1, const struct tm *tm_2) {
  double year_distance = abs(tm_1->tm_year - tm_2->tm_year) * 365;
  double month_distance = abs(tm_1->tm_mon - tm_2->tm_mon) * 30;
  double day_distance = abs(tm_1->tm_mday - tm_2->tm_mday);
  return year_distance + month_distance + day_distance;
}

double TmDatetimeDistance(const struct tm *tm_1, const struct tm *tm_2) {
  double day_distance = TmDateDistance(tm_1, tm_2) * 24 * 60 * 60;
  double hour_distance = abs(tm_1->tm_hour - tm_2->tm_hour) * 60 * 60;
  double minute_distance = abs(tm_1->tm_min - tm_2->tm_min) * 60;
  double second_distance = abs(tm_1->tm_sec - tm_2->tm_sec);
  return day_distance + hour_distance + minute_distance + second_distance;
}

typedef struct SwapDistanceCoordinates {
  double start_distance;
  double start_weight;
  double end_distance;
  double end_weight;
  double trade_time_distance;
  double trade_time_weight;
  double fixed_rate_distance;
  double fixed_rate_weight;
  double notional_distance;
  double notional_weight;
  double ref_rate_distance;
  double ref_rate_weight;
  double fixed_freq_distance;
  double fixed_freq_weight;
  double float_freq_distance;
  double float_freq_weight;
} SwapDistanceCoordinates;

SwapDistanceCoordinates GetSwapDistanceCoordinates(const Swap swap_1,
                                                   const Swap swap_2) {
  SwapDistanceCoordinates swap_distance;
  swap_distance.start_distance =
      TmDateDistance(&(swap_1.start_date), &(swap_2.start_date));
  swap_distance.end_distance =
      TmDateDistance(&(swap_1.end_date), &(swap_2.end_date));
  swap_distance.trade_time_distance =
      TmDatetimeDistance(&(swap_1.trade_time), &(swap_2.trade_time));
  swap_distance.fixed_rate_distance =
      abs(swap_1.fixed_rate - swap_2.fixed_rate);
  swap_distance.notional_distance = abs(swap_1.notional - swap_2.notional);
  swap_distance.ref_rate_distance =
      (swap_1.ref_rate == swap_2.ref_rate) ? 0 : 1;
  swap_distance.fixed_freq_distance =
      (swap_1.fixed_pay_freq == swap_2.fixed_pay_freq) ? 0 : 1;
  swap_distance.float_freq_distance =
      (swap_1.float_pay_freq == swap_2.float_pay_freq) ? 0 : 1;
  return swap_distance;
}

void InitSwapDistanceCoordinatesWeights(
    SwapDistanceCoordinates *swap_distance, double start_weight,
    double end_weight, double trade_time_weight, double fixed_rate_weight,
    double notional_weight, double ref_rate_weight, double fixed_freq_weight,
    double float_freq_weight) {
  swap_distance->start_weight = start_weight;
  swap_distance->end_weight = end_weight;
  swap_distance->trade_time_weight = trade_time_weight;
  swap_distance->fixed_rate_weight = fixed_rate_weight;
  swap_distance->notional_weight = notional_weight;
  swap_distance->ref_rate_weight = ref_rate_weight;
  swap_distance->fixed_freq_weight = fixed_freq_weight;
  swap_distance->float_freq_weight = float_freq_weight;
}

double L2Distance(SwapDistanceCoordinates swap_distance) {
  return swap_distance.start_distance * swap_distance.start_distance *
             swap_distance.start_weight +
         swap_distance.end_distance * swap_distance.end_distance *
             swap_distance.end_weight +
         swap_distance.trade_time_distance * swap_distance.trade_time_distance *
             swap_distance.trade_time_weight +
         swap_distance.fixed_rate_distance * swap_distance.fixed_rate_distance *
             swap_distance.fixed_rate_weight +
         swap_distance.notional_distance * swap_distance.notional_distance *
             swap_distance.notional_weight;
}

double L1Distance(SwapDistanceCoordinates swap_distance) {
  return swap_distance.start_distance * swap_distance.start_weight +
         swap_distance.end_distance * swap_distance.end_distance +
         swap_distance.trade_time_distance * swap_distance.trade_time_weight +
         swap_distance.fixed_rate_distance * swap_distance.fixed_rate_distance +
         swap_distance.notional_distance * swap_distance.notional_weight;
}

void InitSwapDistanceCoordinatesDefaultWeights(
    SwapDistanceCoordinates *distance_struct) {
  InitSwapDistanceCoordinatesWeights(distance_struct, 1.0 / 365.0, 1.0 / 365.0,
                                     1.0 / (365.0 * 24 * 60 * 60), 100,
                                     1.0 / (10000000), 0, 0, 0);
}

/*** Parsing utils ***/
Swap ListStringToSwap(const char *input, size_t input_size) {
  // Expect a list to be passed in like "Colname:Value;"
  Swap swap = {0};
  const size_t max_buff_size = 64;
  char buff[max_buff_size], attr_name[max_buff_size], attr_value[max_buff_size];
  size_t buff_idx = 0, attr_name_idx = 0, attr_value_idx = 0;
  size_t current_input_idx = 0;
  char this_char = 0;
  char reading_attr_name = 1;
  while (current_input_idx < input_size) {
    buff_idx = 0, attr_name_idx = 0, attr_value_idx = 0;
    while (((this_char = input[current_input_idx++]) != ';') &&
           (current_input_idx < input_size)) {
      buff[buff_idx++] = this_char;
      if (buff_idx > max_buff_size)
        Die("Max buffer size reached in ListStringToSwap");
      if ((reading_attr_name == 1) && (this_char != ':')) {
        attr_name[attr_name_idx++] = this_char;
      } else if (reading_attr_name == 1) {
        attr_name[attr_name_idx] = '\0';
        reading_attr_name = 0;
      } else {
        attr_value[attr_value_idx++] = this_char;
      }
    }
    attr_value[attr_value_idx] = '\0';
    buff[buff_idx] = '\0';
    enum AttrToParse parsed_attr_name = EvaluateColname(attr_name);
    AssignSwapValue(&swap, parsed_attr_name, attr_value);
  }
  return swap;
}

void SwapToListString(StringBuffer *output_string, Swap *swap_p) {
  char value_buffer[64];
  sprintf(value_buffer, "ID:%ld;", swap_p->id);
  StringAppend(output_string, value_buffer);
  StringAppend(output_string, "StartDate:");
  DateFromTm(value_buffer, swap_p->start_date);
  StringAppend(output_string, value_buffer);
  StringAppend(output_string, ";");
  StringAppend(output_string, "EndDate:");
  DateFromTm(value_buffer, swap_p->end_date);
  StringAppend(output_string, value_buffer);
  StringAppend(output_string, ";");
  sprintf(value_buffer, "FixedRate:%lf;", swap_p->fixed_rate);
  StringAppend(output_string, value_buffer);
  sprintf(value_buffer, "Notional:%lf;", swap_p->notional);
  StringAppend(output_string, value_buffer);
  switch (swap_p->ref_rate) {
    case USSOFR:
      StringAppend(output_string, "RefRate:USSOFR;");
      break;
    case USLIBOR:
      StringAppend(output_string, "RefRate:USLIBOR;");
      break;
    case USCPI:
      StringAppend(output_string, "RefRate:USCPI");
      break;
    case USSTERM:
      StringAppend(output_string, "RefRate:USTERM");
      break;
    case REF_RATE_ERROR:
      StringAppend(output_string, "RefRate:ERROR");
      break;
  }
  sprintf(value_buffer, "FixedFreq:%d;", swap_p->fixed_pay_freq);
  StringAppend(output_string, value_buffer);
  sprintf(value_buffer, "FloatFreq:%d;", swap_p->float_pay_freq);
  StringAppend(output_string, value_buffer);
}

Swap *GetNearestSwapL2(Swap swap, Swap *swap_list, size_t swap_list_size) {
  SwapDistanceCoordinates distance_struct = {0};
  InitSwapDistanceCoordinatesWeights(
      &distance_struct,
      (swap.start_date.tm_year == 0 ? 0 : 1),  // start_weight
      (swap.end_date.tm_year == 0 ? 0 : 1),    // end_weight
      (swap.trade_time.tm_year == 0 ? 0 : 1),  // trade_time_weight
      (swap.fixed_rate == 0 ? 0 : 1), (swap.notional == 0 ? 0 : 1),
      (swap.ref_rate == REF_RATE_ERROR ? 0 : 1000000),
      (swap.fixed_pay_freq == PAY_FREQ_ERROR ? 0 : 1000000),
      (swap.float_pay_freq == PAY_FREQ_ERROR ? 0 : 1000000));
  double this_distance = 1000000000;
  double nearest_distance = this_distance;
  size_t nearest_idx = 0;
  for (size_t i = 0; i < swap_list_size; i++) {
    SwapDistanceCoordinates distance_struct =
        GetSwapDistanceCoordinates(swap, swap_list[i]);
    this_distance = L2Distance(distance_struct);
    if (this_distance < nearest_distance) {
      nearest_distance = this_distance;
      nearest_idx = i;
    }
  }
  return &swap_list[nearest_idx];
}

Swap SwapFromInputLine(const char *input_line) {
  Swap swap = {0};
  size_t begin_idx = 0;
  size_t separator_idx = 0;
  size_t end_idx = 0;
  char attribute_buffer[64];
  char value_buffer[64];
  AttrToParse this_attribute;
  while (end_idx++ < strlen(input_line)) {
    if (input_line[end_idx] == ';') {
      separator_idx = begin_idx;
      while (input_line[separator_idx] != ':') {
        separator_idx++;
      }
      strncpy(attribute_buffer, input_line + begin_idx,
              separator_idx - begin_idx);
      strncpy(value_buffer, input_line + separator_idx,
              end_idx - separator_idx);
      begin_idx = end_idx + 1;
      this_attribute = EvaluateColname(attribute_buffer);
      AssignSwapValue(&swap, this_attribute, value_buffer);
    }
  }
  return swap;
}

/*** Server functions ***/
void HandleSearchConnection(int connection, int *is_running_p,
                            SwapList swap_list) {
  // Get input
  char buffer[512];
  memset(buffer, 0, sizeof(buffer));
  read(connection, buffer, sizeof(buffer));
  const char *kill_signal = "kill";
  if (strcmp(buffer, kill_signal) == 0) {
    *is_running_p = 0;
  }
  printf("%s", buffer);
  Swap input_swap = {0};
  SwapFromInputLine(buffer);
  Swap *nearest_swap =
      GetNearestSwapL2(input_swap, swap_list.contents, swap_list.size);
  StringBuffer response;
  StringInit(&response);
  SwapToListString(&response, nearest_swap);
  send(connection, response.string, response.length, 0);
}

typedef struct Colnames {
  size_t max_colname_len;
  size_t n_colnames;
  char *contents;
} Colnames;

typedef struct StartupContext {
  Colnames colnames;
  SwapList swap_list;
} StartupContext;

StartupContext LoadSwapsFromFile(const char *filename,
                           int max_n_cols, int chunk_size, int max_colname_len,
                           int max_n_loaded_swaps) {
  Colnames colnames = {0};
  SwapList swap_list = {0};
  swap_list.contents = malloc(max_n_loaded_swaps * sizeof(Swap));
  colnames.contents = malloc(max_n_cols * max_colname_len);
  colnames.max_colname_len = max_colname_len;
  char line_buffer[chunk_size];
  FILE *handler = fopen(filename, "r");
  if (handler) {
    // get column names
    int line_size = ReadLine(handler, line_buffer, chunk_size);
    colnames.n_colnames =
        ParseLine(colnames.contents, line_buffer, line_size, max_colname_len);
    // read swaps into array
    int n_loaded_swaps = 0;
    for (int i = 0; i < max_n_loaded_swaps; i++) {
      line_size = ReadLine(handler, line_buffer, chunk_size);
      if (line_size == 0) {
        break;
      }
      n_loaded_swaps = i + 1;
      swap_list.contents[i] = SwapFromCSVLine(
          line_buffer, line_size, colnames.contents, max_colname_len);
    }
    printf("%d swaps loaded\n", n_loaded_swaps);
    swap_list.size = n_loaded_swaps;
  }
  fclose(handler);
  StartupContext startup_context;
  startup_context.colnames = colnames;
  startup_context.swap_list = swap_list;
  return startup_context;
}

StartupContext LoadFileOnStartup() {
  int max_n_cols = 80;
  int chunk_size = 2048;
  int max_colname_len = 64;
  int max_n_loaded_swaps = 1000;
  const char *filename = "/Users/aionfeehan/Desktop/DTCC/sofr_swaps.csv";
  StartupContext startup_context =
      LoadSwapsFromFile(filename, max_n_cols, chunk_size,
                        max_colname_len, max_n_loaded_swaps);
  return startup_context;
}

int LaunchServer(int port_no, int queue_size) {
  StartupContext context = LoadFileOnStartup();
  int is_running = 1;

  // Create a socket
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) Die("LaunchServer - socket");
  struct linger lin;
  lin.l_onoff = 0;
  lin.l_linger = 0;
  setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char *)&lin, sizeof(int));
  struct sockaddr_in address;

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_no);

  // bind
  struct sockaddr *address_p = (struct sockaddr *)&address;
  size_t address_size = sizeof(address);
  if (bind(sock, address_p, address_size) < 0) Die("LaunchServer - bind");

  // Start listening
  if (listen(sock, queue_size) < 0) Die("LaunchServer - listen");

  while (is_running != 0) {
    // Open connection
    int connection =
        accept(sock, (struct sockaddr *)&address, (socklen_t *)&address_size);
    if (connection < 0) Die("LaunchServer - accept");
    HandleSearchConnection(
        connection, &is_running, context.swap_list);
    close(connection);
  }

  close(sock);

  return 0;
}

int main() {
  // int port_no = 9999;
  // int queue_size = 10;
  // LaunchServer(port_no, queue_size);
  StartupContext context = LoadFileOnStartup();
  Swap input_swap = {0};
  const char *buffer = "Notional Amount 1:250000000;Ref Rate:USSOFR TERM;";
  SwapFromInputLine(buffer);
  Swap *nearest_swap =
      GetNearestSwapL2(input_swap, context.swap_list.contents, context.swap_list.size);
  StringBuffer response;
  StringInit(&response);
  SwapToListString(&response, nearest_swap);
  printf("%s\n", response.string);
  return 0;
}
