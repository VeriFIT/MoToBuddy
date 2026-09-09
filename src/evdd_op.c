/**
 * @file evdd_op.c
 * @brief EVDD operator implementations and computed tables.
 *
 * Counterpart of mtbdd_apply_*, mtbdd_ite, and mtbdd_operation in mtbdd.c
 * and mtbdd_operator_init in bddop.c. Shannon split on LEVEL; compose
 * incoming weights; leaf op is the value algebra.
 */

#include "evdd_op.h"
