//Copyright (c) 2004-2020 Microchip Technology Inc. and its subsidiaries.
//SPDX-License-Identifier: MIT



#define LOOPDETECT_DEBUG        (0)

#include "common.h"     /* Always include common.h at the first place of user-defined herder files */

#include "ledtsk.h"
#include "vtss_luton26_reg.h"
#include "h2io.h"
#include "h2.h"
#include "timer.h"
#include "main.h"
#include "phydrv.h"
#include "phytsk.h"
#include "phymap.h"
#include "hwport.h"
#include "misc2.h"
#if LOOPDETECT_DEBUG
#include "print.h"
#endif

#if TRANSIT_LOOPDETECT

/*****************************************************************************
 *
 *
 * Defines
 *
 *
 *
 ****************************************************************************/

#define LOOPBACK_AGE_TIMEOUT    (100)

#define LOOP_DETECT_MAX         (2)

/*****************************************************************************
 *
 *
 * Typedefs and enums
 *
 *
 *
 ****************************************************************************/

typedef enum {
    IDLE,
    MONITORING,
    LOOPING
} state_t;

enum {
    VTSS_PGID_DEST_MASK_START   =   0,
    VTSS_PGID_AGGR_MASK_START   =  64,
    VTSS_PGID_SOURCE_MASK_START =  80
};

/*****************************************************************************
 *
 *
 * Prototypes for local functions
 *
 *
 *
 ****************************************************************************/


/*****************************************************************************
 *
 *
 * Local data
 *
 *
 *
 ****************************************************************************/

/* Boot up status is IDLE */
static state_t          g_state                     = IDLE;

static bit              ldet_aging_timer_started    = 0;
static uchar            loopback_aging_timeout      = LOOPBACK_AGE_TIMEOUT;

static port_bit_mask_t  log_block_mask              = 0;
static uchar xdata      loop_count [NO_OF_PORTS];

static bit              led_on_flag                 = 0;

static port_bit_mask_t  log_blocked_mask            = 0;
static port_bit_mask_t  cpu_learned_loop_mask       = 0;

/*****************************************************************************
 *
 *
 * Local functins
 *
 *
 *
 ****************************************************************************/

#if LOOPDETECT_DEBUG
static void ldet_print_port_mask(char *str, port_bit_mask_t mask)
{
    print_str(str);
    print_hex_prefix();
    print_hex_dw(mask);
    print_cr_lf();
}
#endif

static void ldet_status_set (state_t new_state)
{
    if (new_state != g_state) {
#if LOOPDETECT_DEBUG
        print_str("ldet: state ");
        print_dec(g_state);
        print_str(" -> ");
        print_dec(new_state);
        print_cr_lf();
#endif
    }

    g_state = new_state;
}

#if LOOPDETECT_DEBUG
static state_t ldet_status_get ( void )
{
    return g_state;
}
#endif

static void ldet_down_ports_clear (
    port_bit_mask_t         link_mask
) {
    vtss_port_no_t          port_no;
    vtss_port_no_t          i_port_no;

    for (port_no = 1; port_no <= NO_OF_PORTS; port_no++)
    {
        i_port_no = port2int(port_no);

        if (TEST_PORT_BIT_MASK(i_port_no, &link_mask))
            continue;

        // clear the counter when the port is linkdown
        loop_count[ port_no - 1 ] = 0;

        //clear mask if link down
        WRITE_PORT_BIT_MASK(i_port_no, 0, &log_block_mask);

        //clear mask if link down
        WRITE_PORT_BIT_MASK(i_port_no, 0, &cpu_learned_loop_mask);
    }
}

static void ldet_timer_refresh (void)
{
    /* loop might happend, start to monitor if happened again in 10 second */
    ldet_aging_timer_started   = 1;
    loopback_aging_timeout     = LOOPBACK_AGE_TIMEOUT;  // Refresh the timer
}

/*****************************************************************************
 *
 *
 * Public API functins
 *
 *
 *
 ****************************************************************************/

void ldettsk ( void )
{
    port_bit_mask_t         move_mask;
    port_bit_mask_t         link_mask;

    vtss_port_no_t          port_no;
    vtss_port_no_t          i_port_no;
    uchar                   lp_eab;

    /*
     * 1. Read current move mask
     */

    H2_READ (VTSS_ANA_ANA_TABLES_ANMOVED, move_mask);
    H2_WRITE(VTSS_ANA_ANA_TABLES_ANMOVED, 0);  // Clear the counter

    /*
     * 2. Read link mask and clear counters and masks if a port is linked down.
     */

    link_mask = phy_get_link_mask();

    ldet_down_ports_clear(link_mask);

    /*
     * 3. If there is a *possible* loop condition, start/refresh timer.
     */

    if (move_mask || cpu_learned_loop_mask)
    {
#if LOOPDETECT_DEBUG
        if (!ldet_aging_timer_started)
        {
            if (move_mask)
                ldet_print_port_mask("ldet: move mask: ", move_mask);

            if (cpu_learned_loop_mask)
                ldet_print_port_mask("ldet: self mac: ", cpu_learned_loop_mask);
        }
#endif
        ldet_timer_refresh();
        ldet_status_set( MONITORING );
    }

    /*
     * 4. If timer is started and we found:
     *    4.1 Possible loop of MAC address move on some ports.
     *        - Increment the counter of those ports.
     *        - If counter exceeds LOOP_DETECT_MAX, consider it as a loop.
     *    4.2 Found the MAC address of myself from some ports.
     *        - Consider it as a loop.
     */

    if (ldet_aging_timer_started && (move_mask || cpu_learned_loop_mask))
    {
        for (port_no = 1; port_no <= NO_OF_PORTS; port_no++)
        {
            i_port_no = port2int(port_no);

            if ( TEST_PORT_BIT_MASK(i_port_no, &cpu_learned_loop_mask) )
            {
#if LOOPDETECT_DEBUG
                if ( ldet_status_get() != LOOPING ) {
                    print_str("ldet: looped at ");
                    print_dec(port_no);
                    print_cr_lf();
                }
#endif
                WRITE_PORT_BIT_MASK(i_port_no, 1, &log_block_mask);  //Local looped
                loop_count[ port_no - 1 ] = 0;
                WRITE_PORT_BIT_MASK(i_port_no, 0, &cpu_learned_loop_mask); //clear mask after log

                ldet_status_set ( LOOPING );
            }
            else if ( TEST_PORT_BIT_MASK( i_port_no, &move_mask ) )
            {
                if ( loop_count[ port_no - 1] < LOOP_DETECT_MAX )
                {
                    /* Update counter block */
                    loop_count[ port_no - 1]++;
                }
                else
                {
#if LOOPDETECT_DEBUG
                    if ( ldet_status_get() != LOOPING ) {
                        print_str("ldet: looped at ");
                        print_dec(port_no);
                        print_cr_lf();
                    }
#endif
                    WRITE_PORT_BIT_MASK(i_port_no, 1, &log_block_mask);

                    loop_count[port_no - 1] = 0;

                    ldet_status_set ( LOOPING );
                }
            }
        }
    }

    /*
     * 5. If the new block mask may have changed (log_block_mask) from old
     *    mask (log_blocked_mask), update it to LED.
     */

    if ( log_block_mask != log_blocked_mask )
    {
        log_blocked_mask |= log_block_mask;

        for (port_no = 1; port_no <= NO_OF_PORTS; port_no++)
        {
            vtss_led_mode_type_t    led_mode;

            i_port_no = port2int(port_no);

            if (!TEST_PORT_BIT_MASK(i_port_no, &link_mask)) {
                WRITE_PORT_BIT_MASK(i_port_no, 0, &log_blocked_mask);
            }

#if FRONT_LED_PRESENT
            led_mode = TEST_PORT_BIT_MASK(i_port_no, &log_blocked_mask) ?
                                          VTSS_LED_MODE_OFF :
                                          VTSS_LED_MODE_BLINK_YELLOW;

            led_state_set(port_no, VTSS_LED_EVENT_CABLE, led_mode);
#endif
        }
        led_on_flag = 1;
#if LOOPDETECT_DEBUG
        print_str("update blink ...\r\n");
#endif
    }
    else if ( log_blocked_mask )
    {
        if (!led_on_flag)
        {
            for (port_no = 1; port_no <= NO_OF_PORTS; port_no++)
            {
                i_port_no = port2int(port_no);
                if (!TEST_PORT_BIT_MASK(i_port_no, &link_mask)) {
                    WRITE_PORT_BIT_MASK(i_port_no, 0, &log_blocked_mask);
                }
#if FRONT_LED_PRESENT
                if (TEST_PORT_BIT_MASK(i_port_no, &log_blocked_mask)) {
                    led_state_set(port_no, VTSS_LED_EVENT_CABLE, VTSS_LED_MODE_OFF);
                } else {
                    led_state_set(port_no, VTSS_LED_EVENT_CABLE, VTSS_LED_MODE_BLINK_YELLOW);
                }
#endif
            }
            led_on_flag = 1;
#if LOOPDETECT_DEBUG
            print_str("blink ...\r\n");
#endif
        }
    }

    /*
     * 6. If no loop happened again, stop blinking
     */

    if (!ldet_aging_timer_started) {
        if (led_on_flag) {
#if FRONT_LED_PRESENT
            for (port_no = 1; port_no <= NO_OF_PORTS; port_no++) {
                led_state_set(port_no, VTSS_LED_EVENT_CABLE, VTSS_LED_MODE_NORMAL);
            }
#endif
            led_on_flag = 0;
#if LOOPDETECT_DEBUG
            ldet_status_set ( IDLE );
#endif
        }

        for (port_no = 1; port_no <= NO_OF_PORTS; port_no++) {
            i_port_no = port2int(port_no);
            loop_count[port_no - 1] = 0;
        }

        log_block_mask   = 0x0; // Clear the loop port mask record
        log_blocked_mask = 0x0;
    }


    /*
     * 7. Block/Unblock port forwarding.
     */

    lp_eab = 1;

    for ( port_no = 1; port_no <= NO_OF_PORTS; port_no++ )
    {
#if LOOPDETECT_DEBUG
        static port_bit_mask_t      src_fwd_mask_backup[ NO_OF_PORTS ];
        port_bit_mask_t             src_fwd_mask;
#endif
        unsigned long               addr;
        port_bit_mask_t             inv_mask;

        i_port_no = port2int(port_no);
        addr      = VTSS_ANA_ANA_TABLES_PGID(i_port_no + VTSS_PGID_SOURCE_MASK_START);
        inv_mask  = ~PORT_BIT_MASK(i_port_no) & link_mask;

        if (lp_eab)
            inv_mask &= ~log_blocked_mask;

        H2_WRITE_MASKED(addr, inv_mask, ALL_PORTS);

#if LOOPDETECT_DEBUG
        H2_READ(addr, src_fwd_mask);

        if ( src_fwd_mask_backup[ port_no - 1 ] != src_fwd_mask ) {
            print_str("ldet: port ");
            print_dec(port_no);
            ldet_print_port_mask(" forwarding: ", src_fwd_mask);
            src_fwd_mask_backup[ port_no - 1 ] = src_fwd_mask;
        }
#endif
    }
}


void ldet_aging_100ms (void)
{
    if (ldet_aging_timer_started) {
        loopback_aging_timeout--;
        if (loopback_aging_timeout == 0) {
            ldet_aging_timer_started = 0;
        }
    }
}


void ldet_add_cpu_found (vtss_port_no_t i_port_no)
{
#if LOOPDETECT_DEBUG
    if (!TEST_PORT_BIT_MASK(i_port_no, &cpu_learned_loop_mask)) {
        print_str("ldet: found self on port ");
        print_dec(port2ext(i_port_no));
        print_cr_lf();
    }
#endif
    WRITE_PORT_BIT_MASK(i_port_no, 1, &cpu_learned_loop_mask);
}


#endif /* TRANSIT_LOOPDETECT */
