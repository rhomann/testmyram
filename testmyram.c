/*
 * testmyram -- Simple RAM testing program
 * Copyright (C) 2011  Robert Homann
 *
 * This file is part of testmyram.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libfid, the Full-text Index Data structure library, and this
 * program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>

#include "prng.h"

#define PACKAGE_VERSION   "0.1"
#define PACKAGE_URL       "https://github.com/rhomann/testmyram"

typedef struct
{
  uint16_t num_of_blocks;
  size_t words_per_block;
  uint32_t **blocks;

  uint32_t iterations_left;
  uint32_t read_iterations;
  uint32_t rand_seed;
  prng_state_t rand_state;

  struct timespec fade_time;

  int return_value;
  uint16_t job_id;
} Inst;

typedef struct
{
  uint16_t fade_seconds;
  uint16_t num_of_blocks;
  size_t size_per_block;
  uint32_t total_iterations;
  uint32_t read_iterations;
  uint16_t instances;
} Config;

static int program_running = 1;
static int verbose_level;

__attribute__ ((format (printf, 2, 3) ))
static void verbose(int level, const char *fmt, ...)
{
  if(level > verbose_level)
    return;

  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

static void *xmalloc(size_t size)
{
  for(int tries = 5; tries > 0; --tries)
  {
    void *ptr = malloc(size);

    if(ptr != NULL)
      return ptr;

    static const struct timespec ts = { .tv_nsec = 100UL * 1000UL * 1000UL };
    struct timespec remaining = ts;

    while(nanosleep(&remaining, &remaining) == EINTR)
      ;
  }

  fprintf(stderr, "Failed to allocate %zu bytes.\n", size);
  return NULL;
}

static void sighandler(int sig)
{
  program_running = 0;
}

static void setup_signals(void)
{
  static const struct sigaction action =
  {
    .sa_handler = sighandler,
    .sa_flags = SA_RESETHAND,
  };

  sigaction(SIGINT, &action, NULL);
}

static void free_instance(Inst *inst)
{
  if(inst->blocks == NULL)
    return;

  for(size_t i = 0; i < inst->num_of_blocks; ++i)
  {
    if(inst->blocks[i] != NULL)
      free(inst->blocks[i]);
  }

  free(inst->blocks);
}

static int init_instance(Inst *inst, const Config *config, uint16_t job_id)
{
  inst->iterations_left = config->total_iterations;
  inst->read_iterations = config->read_iterations;
  inst->num_of_blocks = config->num_of_blocks;
  inst->words_per_block = config->size_per_block / sizeof(uint32_t);
  inst->fade_time.tv_sec = config->fade_seconds;
  inst->fade_time.tv_nsec = 0;
  inst->return_value = -1;
  inst->job_id = job_id;

  if((inst->blocks = xmalloc(sizeof(uint32_t *) * inst->num_of_blocks)) == NULL)
    return -1;

  memset(inst->blocks, 0, sizeof(uint32_t *) * inst->num_of_blocks);

  for(size_t i = 0; i < inst->num_of_blocks; ++i)
  {
    if((inst->blocks[i] =
        xmalloc(inst->words_per_block * sizeof(uint32_t))) == NULL)
    {
      free_instance(inst);
      return -1;
    }
  }

  return 0;
}

static void fill_block(uint32_t *restrict block, size_t n,
                       prng_state_t *restrict state)
{
  for(size_t i = 0; i < n; ++i)
    block[i] = prng_next(state);
}

static int check_block(const uint32_t *restrict block, size_t n,
                       prng_state_t *restrict state, const char *restrict str)
{
  int ret = 0;
  uint32_t temp;

  for(const uint32_t *last = block + n; block < last; ++block)
  {
    temp = prng_next(state);
    if(*block != temp)
    {
      fprintf(stderr, "%sUnexpected memory content 0x%08" PRIx32 " "
              "at %p, expected 0x%08" PRIx32 ".\n", str, *block, block, temp);
      ret = -1;
    }
  }

  return ret;
}

static void memory_fade_delay(struct timespec tspec, const char *str)
{
  if(tspec.tv_sec == 0 && tspec.tv_nsec == 0)
    return;

  verbose(2, "%sWaiting to capture possible memory fade effect...\n", str);
  while(nanosleep(&tspec, &tspec) == EINTR && program_running)
    ;
}

static const char *print_job_id(uint16_t id, char *buffer)
{
  if(id == 0)
    *buffer = '\0';
  else
    snprintf(buffer, 16, "[%" PRIu16 "] ", id);

  return buffer;
}

static void *perform_memtest(void *arg)
{
  Inst *inst = (Inst *)arg;
  unsigned long total_iterations = 0;
  char buffer[16];

  while(program_running && inst->iterations_left > 0)
  {
    ++total_iterations;
    if(inst->job_id == 0)
      verbose(1, "### Iteration %lu ###\n", total_iterations);
    else
      verbose(1, "### Iteration %lu in thread %" PRIu16 " ###\n",
              total_iterations, inst->job_id);

    verbose(2, "%sFilling %zu bytes distributed over %" PRIu16 " blocks.\n",
            print_job_id(inst->job_id, buffer),
            inst->num_of_blocks * inst->words_per_block * sizeof(uint32_t),
            inst->num_of_blocks);

    inst->rand_seed = prng_init_from_dev_random(&inst->rand_state);

    for(size_t i = 0; i < inst->num_of_blocks; ++i)
      fill_block(inst->blocks[i], inst->words_per_block, &inst->rand_state);

    for(uint32_t ri = inst->read_iterations; ri > 0; --ri)
    {
      memory_fade_delay(inst->fade_time, print_job_id(inst->job_id, buffer));

      if(!program_running)
        verbose(0, "%sInterrupted, terminating after next comparison.\n",
                print_job_id(inst->job_id, buffer));

      verbose(2, "%sComparing %zu bytes.\n",
              print_job_id(inst->job_id, buffer),
              inst->num_of_blocks * inst->words_per_block * sizeof(uint32_t));

      prng_init(inst->rand_seed, &inst->rand_state);

      for(size_t i = 0; i < inst->num_of_blocks; ++i)
        if(check_block(inst->blocks[i], inst->words_per_block,
                       &inst->rand_state,
                       print_job_id(inst->job_id, buffer)) != 0)
          inst->return_value = -2;

      if(!program_running)
        break;
    }

    if(inst->iterations_left < UINT32_MAX)
      --inst->iterations_left;
  }

  /* this turns the return value to either 0 (success) or -1 (failure) */
  ++inst->return_value;

  return inst;
}

static int run_single_instance(const Config *config)
{
  Inst inst;

  if(init_instance(&inst, config, 0) != 0)
    return -1;

  perform_memtest(&inst);

  free_instance(&inst);
  return inst.return_value;
}

static int run_threaded(const Config *config)
{
  pthread_t *threads = xmalloc(sizeof(pthread_t) * config->instances);

  if(threads == NULL)
    return -1;

  uint16_t num_threads = 0;

  for(/* nothing */; num_threads < config->instances; ++num_threads)
  {
    Inst *inst = xmalloc(sizeof(Inst));

    if(inst == NULL)
      break;

    if(init_instance(inst, config, num_threads + 1) != 0)
    {
      free(inst);
      break;
    }

    if(pthread_create(&threads[num_threads], NULL,
                      perform_memtest, inst) != 0)
    {
      free_instance(inst);
      free(inst);
      break;
    }
  }

  int ret = 0;

  if(num_threads == 0)
  {
    fprintf(stderr, "Couldn't create any thread.\n");
    goto exit_error;
  }
  else if(num_threads < config->instances)
    verbose(0, "Started %" PRIu16 " of %" PRIu16 " "
            "requested parallel checks.\n", num_threads, config->instances);
  else
    verbose(1, "Started %" PRIu16 " parallel checks.\n", num_threads);

  for(uint16_t i = 0; i < num_threads; ++i)
  {
    void *retptr = NULL;

    if(pthread_join(threads[i], &retptr) != 0 || retptr == NULL)
    {
      fprintf(stderr, "Failed joining thread %" PRIu16 ".\n", i);
      ret = -1;
      continue;
    }

    Inst *inst = (Inst *)retptr;

    if(inst->return_value != 0)
      ret = -1;

    free_instance(inst);
    free(inst);
  }

exit_error:
  free(threads);
  return ret;
}

static int parse_uint32(const char *str, uint32_t *value)
{
  char *endptr = NULL;
  unsigned long val = strtoul(str, &endptr, 10);

  if(endptr == NULL || *endptr != '\0')
    fprintf(stderr, "Could not convert \"%s\" to number.\n", str);
  else if(val == ULONG_MAX && errno == ERANGE)
    fprintf(stderr, "Converted value \"%s\" too large.\n", str);
  else
  {
    *value = val;
    return 0;
  }

  return -1;
}

static void usage(const char *prgname, int full_info, int options)
{
  printf("\
testmyram " PACKAGE_VERSION " -- Simple RAM testing program\n\
Copyright (C) 2011  Robert Homann\n\
\n");

  if(full_info)
    printf("\
This program is free software; you can redistribute it and/or modify it under\n\
the terms of the GNU General Public License as published by the Free Software\n\
Foundation; either version 2, or (at your option) any later version.\n\
\n\
This program is distributed in the hope that it will be useful, but WITHOUT\n\
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS\n\
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.\n\
You should have received a copy of the GNU General Public License along with\n\
this program (see the file COPYING); if not, write to the Free Software\n\
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.\n\
\n\
Get the latest version from " PACKAGE_URL "\n");
  else
    printf("\
This program is distributed in the hope that it will be useful, but WITHOUT\n\
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS\n\
FOR A PARTICULAR PURPOSE. See testmyram -V for details and license information.\n");

  if(options)
    printf("\n\
Usage: %s [options]\n\
\n\
Options:\n\
-n num   Number of memory blocks (default: 10).\n\
-s size  Size per memory block (default: 1 MiB).\n\
-d sec   Number of seconds to wait between write and read phase (default: 0).\n\
-r iter  Number of read iterations after writing (default: 1).\n\
-i iter  Number of iterations (default: unlimited).\n\
-j num   Number of checks running in parallel (default: 1). Parameters above\n\
         are per thread.\n\
-v       Verbose execution.\n\
-V       Show version and license information.\n\
-h       This help screen.\n\
", prgname);
}

static int commandline(int argc, char *argv[], Config *config)
{
  uint32_t temp;
  int c = 0;

  while((c = getopt(argc, argv, "d:hi:j:n:r:s:vV")) != -1)
  {
    switch(c)
    {
     case '?':
      fprintf(stderr, "Use -h for help.\n");
      return -1;

     case 'h':
      return 1;

     case 'V':
      return 2;

     case 'v':
      ++verbose_level;
      break;

     case 'd':
      if(parse_uint32(optarg, &temp) == -1 || temp > UINT16_MAX)
      {
        fprintf(stderr, "The memory fade delay must not exceed %" PRIu16 ".\n",
                UINT16_MAX);
        return -1;
      }

      config->fade_seconds = (uint16_t)temp;
      break;

     case 'i':
      if(parse_uint32(optarg, &temp) == -1 || temp == 0 || temp == UINT32_MAX)
      {
        fprintf(stderr, "The number of iterations must be a "
                "positive value smaller than %" PRIu32 ".\n", UINT32_MAX);
        return -1;
      }

      config->total_iterations = temp;
      break;

     case 'j':
      if(parse_uint32(optarg, &temp) == -1 || temp == 0 || temp > UINT16_MAX)
      {
        fprintf(stderr, "The number of parallel checks must be a "
                "positive value not exceeding %" PRIu16 ".\n", UINT16_MAX);
        return -1;
      }

      config->instances = (uint16_t)temp;
      break;

     case 'n':
      if(parse_uint32(optarg, &temp) == -1 || temp == 0 || temp > UINT16_MAX)
      {
        fprintf(stderr, "The number of blocks must be a "
                "positive value not exceeding %" PRIu16 ".\n", UINT16_MAX);
        return -1;
      }

      config->num_of_blocks = (uint16_t)temp;
      break;

     case 'r':
      if(parse_uint32(optarg, &temp) == -1 || temp == 0)
      {
        fprintf(stderr, "The number of read iterations must be a "
                "positive value.\n");
        return -1;
      }

      config->read_iterations = temp;
      break;

     case 's':
      if(parse_uint32(optarg, &temp) == -1 || temp < sizeof(uint32_t))
      {
        fprintf(stderr, "The memory block size must be at least %zu.\n",
                sizeof(uint32_t));
        return -1;
      }

      config->size_per_block = temp & ~(uint32_t)(sizeof(uint32_t) - 1);
      break;

     default:
      goto error_exit;
    }
  }

error_exit:
  if(optind < argc)
  {
    fprintf(stderr, "Invalid command line. Use -h for help.\n");
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  static Config config =
  {
    .fade_seconds = 0,
    .num_of_blocks = 10,
    .size_per_block = 1 * 1024UL * 1024UL,
    .total_iterations = UINT32_MAX,
    .read_iterations = 1,
    .instances = 1,
  };

  switch(commandline(argc, argv, &config))
  {
   case 0:
    break;

   case 1:
    usage(argv[0], 0, 1);
    return EXIT_SUCCESS;

   case 2:
    usage(argv[0], 1, 0);
    return EXIT_SUCCESS;

   default:
    return EXIT_FAILURE;
  }

  setup_signals();

  verbose(1, "Using a total of %zu bytes distributed over %" PRIu16 " blocks "
          "in %" PRIu16 " threads.\n",
          config.num_of_blocks * config.size_per_block * config.instances,
          config.num_of_blocks, config.instances);

  int ret;

  if(config.instances == 1)
    ret = run_single_instance(&config);
  else
    ret = run_threaded(&config);

  if(ret == 0)
  {
    verbose(0, "OK.\n");
    return EXIT_SUCCESS;
  }

  fprintf(stderr, "Memory check failed.\n");
  return EXIT_FAILURE;
}
