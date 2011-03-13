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

#include <stdio.h>

#include "prng.h"

void prng_init(uint32_t seed, prng_state_t *state)
{
  state->s.state =
    (uint64_t)(seed & 0x0000ffff) | (uint64_t)(seed & 0xffff0000) << 16;
}

uint32_t prng_init_from_dev_random(prng_state_t *state)
{
  uint32_t seed = 0x02300420;
  FILE *file;

  if((file = fopen(DEV_RANDOM, "r")) != NULL)
  {
    fread(&seed, sizeof(uint32_t), (size_t)1, file);
    fclose(file);
  }

  prng_init(seed, state);
  return seed;
}

/*
 * This is a simple LCG implementation. Nothing fancy, but certainly enough for
 * our purposes.
 */
uint32_t prng_next(prng_state_t *state)
{
  static const uint64_t a = 6364136223846793005;
  static const uint64_t c = 1442695040888963407;

  state->s.state = a * state->s.state + c;
  return state->s.value;
}
