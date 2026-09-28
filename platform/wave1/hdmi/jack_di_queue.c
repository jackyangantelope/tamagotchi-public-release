#include "hstx_packet.h"
#define DI_RING_BUFFER_SIZE 128
static hstx_data_island_t jack_data_islands[DI_RING_BUFFER_SIZE];
#define HSTX_DI_RING_ADDRESS jack_data_islands
#include "hstx_data_island_queue.c"
