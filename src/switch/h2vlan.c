//Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
//SPDX-License-Identifier: MIT


#include "common.h"     /* Always include common.h at the first place of user-defined herder files */
#include "main.h"
#include "timer.h"
#include "vtss_luton26_reg.h"
#include "h2io.h"
#include "h2vlan.h"
#include "misc2.h"

/*****************************************************************************
 *
 *
 * Defines
 *
 *
 *
 ****************************************************************************/

/* VLAN table commands */
#define VLAN_TAB_IDLE  0
#define VLAN_TAB_READ  1
#define VLAN_TAB_WRITE 2
#define VLAN_TAB_CLEAR 3

/*****************************************************************************
 *
 * Typedefs and enums
 *
 *
 *
 ****************************************************************************/


/*****************************************************************************
 *
 *
 * Prototypes for local functions
 *
 *
 *
 ****************************************************************************/
static void do_command (ulong aces_value);

/*****************************************************************************
 *
 *
 * Local data
 *
 *
 *
 ****************************************************************************/

/* ************************************************************************ */
void h2_vlan_clear_tab (void)
/* ------------------------------------------------------------------------ --
 * Purpose     : Clear vlan table in switch chip.
 * Remarks     :
 * Restrictions:
 * See also    :
 * Example     :
 ****************************************************************************/
{
    do_command(VTSS_F_ANA_ANA_TABLES_VLANACCESS_VLAN_TBL_CMD(VLAN_TAB_CLEAR));
}

static void do_command (ulong aces_value)
{
    ulong cmd;
    H2_WRITE(VTSS_ANA_ANA_TABLES_VLANACCESS, aces_value);
    do {
        H2_READ(VTSS_ANA_ANA_TABLES_VLANACCESS, cmd);
    } while (VTSS_X_ANA_ANA_TABLES_VLANACCESS_VLAN_TBL_CMD(cmd) != VLAN_CMD_IDLE);
}

