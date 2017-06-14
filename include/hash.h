// Several pre-defined functions related to hash operations
// Copyright (C) 2017 Marcus Pinnecke
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
// warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program.
// If not, see <http://www.gnu.org/licenses/>.

#pragma once

// ---------------------------------------------------------------------------------------------------------------------
// D A T A   T Y P E S
// ---------------------------------------------------------------------------------------------------------------------

#include "defs.h"

typedef size_t (*hash_code_fn_t)(const void *capture, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// I D E N T I T Y   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_identity_size_t(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// P E R F E C T   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_perfect_size_t(const void *capture, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// A D D I T I V E   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_additive(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// X O R   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_xor(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// R O T A T I O N   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_rot(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// B E R N S T E I N   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_bernstein(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// M O D I F I E D   B E R N S T E I N   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_bernstein2(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// S H I F T - A D D - X O R   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_sax(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// F N V   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_fnv(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// O N E - A T - A - T I M E   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_oat(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// J S W   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_jsw(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// E L F   H A S H
// ---------------------------------------------------------------------------------------------------------------------

size_t hash_code_elf(const void *_, size_t key_size, const void *key);

// ---------------------------------------------------------------------------------------------------------------------
// J E N K I N S   H A S H
// ---------------------------------------------------------------------------------------------------------------------

typedef struct hash_code_jen_args_t {
    unsigned initval;
} hash_code_jen_args_t;

/* must be of type 'hash_code_jen_args_t' or NULL. In case this parameter is NULL, a default value for jen is taken */
size_t hash_code_jen(const void *capture, size_t key_size, const void *key);