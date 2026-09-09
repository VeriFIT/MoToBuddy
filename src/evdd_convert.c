/**
 * @file evdd_convert.c
 * @brief MTBDD <-> EVDD conversion walks.
 *
 * Cached like apply. Same CUSTOM type_id on both sides when the algebra
 * matches. Mixed EVDD type_ids require a value mapper.
 */

#include "evdd_convert.h"
