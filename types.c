// Column data types
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

// ---------------------------------------------------------------------------------------------------------------------
// I N C L U D E S
// ---------------------------------------------------------------------------------------------------------------------

#include <types.h>
#include <error.h>

// ---------------------------------------------------------------------------------------------------------------------
// I N T E R F A C E  I M P L E M E N T A T I O N
// ---------------------------------------------------------------------------------------------------------------------

size_t type_sizeof(data_type type)
{
    switch (type) {
        case type_single_char:
        case type_single_bool:
        case type_single_uint8:
        case type_single_int8:
        case type_multi_fixed:
        case type_multi_variable:       return 1;
        case type_single_uint16:
        case type_single_int16:         return 2;
        case type_single_uint32:
        case type_single_int32:
        case type_single_float:
        case type_internal_strlen:      return 4;
        case type_internal_undef:
        case type_internal_tuple_id:
        case type_single_uint64:
        case type_single_int64:
        case type_single_double:
        case type_internal_pointer:     return 8;
        default: {
            error(err_bad_type);
            return 0;
        }
    }
}

bool type_is_fixed_size(data_type type)
{
    return (type != type_multi_variable);
}

const char *type_to_string(data_type type)
{
    switch (type) {
        case type_single_char:       return "char";
        case type_single_bool:       return "bool";
        case type_single_uint8:      return "uint8";
        case type_single_int8:       return "int8";
        case type_multi_fixed:       return "fix_charseq";
        case type_multi_variable:    return "var_charseq";
        case type_single_uint16:     return "uint16";
        case type_single_int16:      return "int16";
        case type_single_uint32:     return "uint32";
        case type_single_int32:      return "int32";
        case type_single_float:      return "float";
        case type_internal_strlen:   return "(strlen)";
        case type_internal_undef:    return "(undef)";
        case type_internal_tuple_id: return "(tuple_id)";
        case type_single_uint64:     return "uint64";
        case type_single_int64:      return "int64";
        case type_single_double:     return "double";
        case type_internal_pointer:  return "(pointer)";
        default: {
            error(err_bad_type);
            return "(bad type)";
        }
    }
}