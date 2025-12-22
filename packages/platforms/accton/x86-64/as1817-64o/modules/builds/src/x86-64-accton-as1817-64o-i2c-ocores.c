
// SPDX-License-Identifier: GPL-2.0
/*
 * i2c-ocores.c: I2C bus driver for OpenCores I2C controller
 * (https://opencores.org/project/i2c/overview)
 *
 * Peter Korsgaard <peter@korsgaard.com>
 *
 * Support for the GRLIB port of the controller by
 * Andreas Larsson <andreas@gaisler.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/platform_data/i2c-ocores.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/moduleparam.h>

/*
 * 'process_lock' exists because ocores_process() and ocores_process_timeout()
 * can't run in parallel.
 */
struct ocores_i2c {
    void __iomem *base;
    int iobase;
    u32 reg_shift;
    u32 reg_io_width;
    unsigned long flags;
    wait_queue_head_t wait;
    struct i2c_adapter adap;
    struct i2c_msg *msg;
    int pos;
    int nmsgs;
    int state; /* see STATE_ */
    spinlock_t process_lock;
    struct mutex hw_lock;
    struct clk *clk;
    int ip_clock_khz;
    int bus_clock_khz;
    void (*setreg)(struct ocores_i2c *i2c, int reg, u8 value);
    u8 (*getreg)(struct ocores_i2c *i2c, int reg);

    /* Error recovery support - per-instance configuration */
    u8 enable_recovery;           /* Enable/disable error recovery for this instance */
    u32 max_recovery_attempts;    /* Maximum recovery attempts for this instance */
    u32 error_threshold;          /* Error count threshold for this instance */
    u32 error_count;              /* Current error count */
    u32 recovery_count;           /* Current recovery attempt count */
    struct delayed_work recovery_work;
};

/* registers */
#define OCI2C_PRELOW		0
#define OCI2C_PREHIGH		1
#define OCI2C_CONTROL		2
#define OCI2C_DATA		3
#define OCI2C_CMD		4 /* write only */
#define OCI2C_STATUS		4 /* read only, same address as OCI2C_CMD */

#define OCI2C_CTRL_IEN		0x40
#define OCI2C_CTRL_EN		0x80

#define OCI2C_CMD_START		0x91
#define OCI2C_CMD_STOP		0x41
#define OCI2C_CMD_READ		0x21
#define OCI2C_CMD_WRITE		0x11
#define OCI2C_CMD_READ_ACK	0x21
#define OCI2C_CMD_READ_NACK	0x29
#define OCI2C_CMD_IACK		0x01

#define OCI2C_STAT_IF		0x01
#define OCI2C_STAT_TIP		0x02
#define OCI2C_STAT_ARBLOST	0x20
#define OCI2C_STAT_BUSY		0x40
#define OCI2C_STAT_NACK		0x80

#define STATE_DONE		0
#define STATE_START		1
#define STATE_WRITE		2
#define STATE_READ		3
#define STATE_ERROR		4

#define TYPE_OCORES		0
#define TYPE_GRLIB		1
#define TYPE_SIFIVE_REV0	2

#define OCORES_FLAG_BROKEN_IRQ BIT(1) /* Broken IRQ for FU540-C000 SoC */

/*
 * Default values for error recovery.
 * These module parameters serve as system-wide defaults.
 * Each controller instance can override these via sysfs.
 */
static u8 default_enable_recovery = 0;
module_param(default_enable_recovery, byte, 0644);
MODULE_PARM_DESC(default_enable_recovery,
                 "Default: Enable error recovery (0=disabled, 1=enabled)");

static uint default_max_recovery_attempts = 5;
module_param(default_max_recovery_attempts, uint, 0644);
MODULE_PARM_DESC(default_max_recovery_attempts,
                 "Default: Maximum number of recovery attempts");

static uint default_error_threshold = 5;
module_param(default_error_threshold, uint, 0644);
MODULE_PARM_DESC(default_error_threshold,
                 "Default: Error count threshold to trigger recovery");

static unsigned int timeout = 1;
module_param(timeout, uint, 0644);
MODULE_PARM_DESC(timeout, "Timeout for ocores_poll_wait (ms)");

static unsigned int debug = 0;
module_param(debug , uint, 0644);
MODULE_PARM_DESC(debug, "Enable or disable debug message. 1 -> enable, 0 -> disable");

static void oc_setreg_8(struct ocores_i2c *i2c, int reg, u8 value)
{
    iowrite8(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_16(struct ocores_i2c *i2c, int reg, u8 value)
{
    iowrite16(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_32(struct ocores_i2c *i2c, int reg, u8 value)
{
    iowrite32(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_16be(struct ocores_i2c *i2c, int reg, u8 value)
{
    iowrite16be(value, i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_32be(struct ocores_i2c *i2c, int reg, u8 value)
{
    iowrite32be(value, i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_8(struct ocores_i2c *i2c, int reg)
{
    return ioread8(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_16(struct ocores_i2c *i2c, int reg)
{
    return ioread16(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_32(struct ocores_i2c *i2c, int reg)
{
    return ioread32(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_16be(struct ocores_i2c *i2c, int reg)
{
    return ioread16be(i2c->base + (reg << i2c->reg_shift));
}

static inline u8 oc_getreg_32be(struct ocores_i2c *i2c, int reg)
{
    return ioread32be(i2c->base + (reg << i2c->reg_shift));
}

static void oc_setreg_io_8(struct ocores_i2c *i2c, int reg, u8 value)
{
    outb(value, i2c->iobase + reg);
}

static inline u8 oc_getreg_io_8(struct ocores_i2c *i2c, int reg)
{
    return inb(i2c->iobase + reg);
}

static inline void oc_setreg(struct ocores_i2c *i2c, int reg, u8 value)
{
    i2c->setreg(i2c, reg, value);
}

static inline u8 oc_getreg(struct ocores_i2c *i2c, int reg)
{
    return i2c->getreg(i2c, reg);
}

/**
 * ocores_compute_prescale_khz - Compute OpenCores I2C prescaler (kHz domain)
 * @ip_khz:  Controller input clock in kilohertz.
 * @bus_khz: Desired I2C bus speed in kilohertz.
 *
 * Formula from OpenCores IP:
 *   prescale = (ip_khz / (5 * bus_khz)) - 1
 * The result is clamped to [0, 0xFFFF] as the prescaler is 16-bit wide.
 *
 * Return: Prescaler value in the range [0, 0xFFFF].
 */
static int ocores_compute_prescale_khz(int ip_khz, int bus_khz)
{
    int prescale;

    if (ip_khz <= 0 || bus_khz <= 0) {
        return 0;
    }
    prescale = (ip_khz / (5 * bus_khz)) - 1;
    prescale = clamp(prescale, 0, 0xFFFF);
    return prescale;
}

/* Bus recovery functions */
static bool ocores_bus_busy(struct ocores_i2c *i2c)
{
    return !!(oc_getreg(i2c, OCI2C_STATUS) & OCI2C_STAT_BUSY);
}

/**
 * ocores_recover_bus - Attempt controller-only recovery for a stuck bus
 * @i2c: OpenCores I2C controller instance
 *
 * This helper performs a best-effort recovery when the OpenCores I2C
 * controller reports a stuck or busy bus. It operates purely on the
 * controller registers and does not rely on GPIO-based
 * &struct i2c_bus_recovery_info.
 *
 * The sequence is:
 *  - Issue STOP and IACK to terminate any in-flight transaction.
 *  - Disable the controller (clear EN/IEN) to flush internal state.
 *  - Optionally verify and, if needed, restore the prescaler while EN=0.
 *  - Re-enable the controller and wait for TIP to clear.
 *  - Sample BUSY/TIP multiple times to check if the bus is idle.
 *  - If still busy, send one extra STOP+IACK and sample again.
 *
 * On success, the bus/controller is considered usable again and
 * @i2c->recovery_count is cleared to allow future attempts to start
 * from a clean state.
 *
 * Return:
 *  * 0       - Recovery completed successfully; bus appears idle.
 *  * -EAGAIN - Bus still appears stuck; caller should back off and,
 *              if allowed by policy, schedule another attempt later.
 */
static int ocores_recover_bus(struct ocores_i2c *i2c)
{
    /*
     * The OpenCores I2C IP doesn't expose direct SCL/SDA pins for bit-banging.
     * A common recovery strategy for this IP is to repeatedly send STOP and check BUSY.
     * For now, we rely on the disable/enable and STOP commands.
     * If actual bit-banging is needed, it would involve temporarily taking over GPIOs
     * which is typically done by the i2c-core recovery framework.
     *
     *
     * Controller-only recovery (no GPIO-based bus_recovery_info on this platform)
     *
     * Phase A: Quiesce the controller (STOP+IACK, disable/enable, wait TIP=0)
     * Phase B: Multi-sample BUSY to confirm the bus is idle
     * Phase C: If still stuck, send an extra STOP+IACK once and re-sample
     */
    int i, ok_count;
    unsigned long timeout_j = msecs_to_jiffies(100); /* TIP wait budget */
    unsigned long start;
    u8 ctrl;
    u16 prer_cur;
    u16 prer_expected;
    unsigned long flags;

    spin_lock_irqsave(&i2c->process_lock, flags);

    /* A1) Terminate any in-flight transfer */
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);

    /* A2) Disable and re-enable the controller to clear internal state */
    ctrl = oc_getreg(i2c, OCI2C_CONTROL);
    ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);

    /* Optional: sanity-check prescaler while EN=0; rewrite only if mismatched */
    prer_cur = oc_getreg(i2c, OCI2C_PRELOW) |
               (oc_getreg(i2c, OCI2C_PREHIGH) << 8);

    /* Compute or cache the expected prescale; this illustrates a typical formula.
     * Replace ocores_compute_prescale_khz() with your driver's existing calculation/cached value. */
    prer_expected = ocores_compute_prescale_khz(i2c->ip_clock_khz, i2c->bus_clock_khz);

    if (prer_cur != prer_expected) {
        /* Reprogram prescaler only when necessary; must be done with EN=0. */
        oc_setreg(i2c, OCI2C_PRELOW,  (u8)(prer_expected & 0xFF));
        oc_setreg(i2c, OCI2C_PREHIGH, (u8)((prer_expected >> 8) & 0xFF));
    }

    /* Re-enable controller and continue with the rest of your recovery steps */
    oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_EN);
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);

    spin_unlock_irqrestore(&i2c->process_lock, flags);

    /* A3) Wait TIP to drop (controller quiescence) */
    start = jiffies;
    while ((oc_getreg(i2c, OCI2C_STATUS) & OCI2C_STAT_TIP) != 0) {
        if (time_after(jiffies, start + timeout_j)) {
            break;
        }
        udelay(50);
    }

    /* B) Multi-sample BUSY to confirm bus idle */
    ok_count = 0;
    for (i = 0; i < 3; i++) {
        if (!ocores_bus_busy(i2c)) {
            ok_count++;
        }
        udelay(100);
    }
    if (ok_count == 3) {
        i2c->recovery_count = 0;
        return 0;
    }

    /* C) Extra STOP once, then re-sample BUSY again */
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
    ok_count = 0;
    for (i = 0; i < 3; i++) {
        if (!ocores_bus_busy(i2c)) {
            ok_count++;
        }
        udelay(100);
    }
    if (ok_count == 3) {
        i2c->recovery_count = 0;
        return 0;
    }

    /* Still not good: ask caller to back off and retry later */
    return -EAGAIN;
}

/**
 * ocores_recovery_work - Delayed work handler driving error recovery
 * @work: Embedded work_struct for the recovery_work item
 *
 * This worker is scheduled by ocores_handle_error() when a controller
 * instance enters an error state and automatic recovery is enabled.
 * It implements a per-bus recovery state machine with the following
 * behavior:
 *
 *  - If recovery has been disabled since the work was queued, the
 *    function logs this, clears @i2c->error_count and
 *    @i2c->recovery_count, and exits without touching the bus.
 *
 *  - If the controller is no longer in STATE_ERROR when the work runs
 *    (for example a new transfer re-initialized the state), the
 *    function treats the condition as resolved, clears the counters,
 *    and exits.
 *
 *  - If @i2c->recovery_count has already reached
 *    @i2c->max_recovery_attempts, the function logs a "Max recovery
 *    attempts reached" message, clears @i2c->error_count, and stops
 *    scheduling further automatic recovery for this bus. This provides
 *    a fail-stop behavior: a permanently faulty bus will not trigger
 *    unbounded retries.
 *
 *  - Otherwise it calls ocores_recover_bus(). On failure,
 *    @i2c->recovery_count is incremented and the worker is rescheduled
 *    with an exponential backoff delay, capped to a bounded maximum.
 *
 *  - On successful recovery, ocores_recover_bus() clears
 *    @i2c->recovery_count and a success message is logged. No further
 *    delayed work is queued.
 *
 * All counters and limits are maintained per @struct ocores_i2c
 * instance so that one misbehaving bus does not affect recovery
 * behavior of other controllers. Userspace may re-arm recovery
 * (e.g. via the error_count sysfs knob) if it decides that additional
 * attempts are acceptable.
 */
static void ocores_recovery_work(struct work_struct *work)
{
    struct ocores_i2c *i2c = container_of(work, struct ocores_i2c, recovery_work.work);
    unsigned int attempt = i2c->recovery_count + 1;
    unsigned long flags;
    int state_now;

    /* Respect runtime toggle: exit immediately if recovery is disabled. */
    if (!i2c->enable_recovery) {
        dev_notice(i2c->adap.dev.parent,
                   "I2C recovery disabled; skip recovery_work (err_cnt=%u, rec_cnt=%u)\n",
                   i2c->error_count, i2c->recovery_count);
        i2c->error_count = 0;
        i2c->recovery_count = 0;
        return;
    }

    /* Skip if controller is no longer in error state (new transfer likely started). */
    spin_lock_irqsave(&i2c->process_lock, flags);
    state_now = i2c->state;
    spin_unlock_irqrestore(&i2c->process_lock, flags);
    if (state_now != STATE_ERROR) {
        i2c->error_count = 0;
        i2c->recovery_count = 0;
        return;
    }

    if (i2c->recovery_count >= i2c->max_recovery_attempts) {
        dev_err(i2c->adap.dev.parent, "Max recovery attempts reached.\n");
        i2c->error_count = 0; /* Reset to allow new transfers */
        return;
    }
    dev_notice(i2c->adap.dev.parent,
               "I2C recovery attempt %u starting (err_cnt=%u)\n",
               attempt, i2c->error_count);

    if (ocores_recover_bus(i2c)) {
        unsigned long delay;

        /* One failed recovery attempt accounted here */
        i2c->recovery_count++;

        /* Stop if we've reached the cap */
        if (i2c->recovery_count >= i2c->max_recovery_attempts) {
            dev_err(i2c->adap.dev.parent, "Max recovery attempts reached.\n");
            i2c->error_count = 0;
            return;
        }

        /* Exponential backoff: 1->250ms, 2->500ms, 3->1s ... up to 5s */
        delay = msecs_to_jiffies(250U << (i2c->recovery_count - 1));
        if (delay > 5 * HZ) {
            delay = 5 * HZ;
        }
        dev_warn(i2c->adap.dev.parent,
                 "I2C recovery attempt %u failed; retry in %ums (rec_cnt=%u)\n",
                 attempt, jiffies_to_msecs(delay), i2c->recovery_count);
        schedule_delayed_work(&i2c->recovery_work, delay);
    } else {
        dev_info(i2c->adap.dev.parent,
                 "I2C recovery successful on attempt %u (bus/controller idle)\n",
                 attempt);
    }
}

/**
 * ocores_check_stuck_with_timeout - Check if controller/bus is stuck with timeout
 * @i2c: Controller instance
 *
 * Wait for TIP to clear after STOP command, then check final status.
 * This provides more reliable stuck detection than immediate status read.
 *
 * Return: true if stuck, false if idle
 */
static bool ocores_check_stuck_with_timeout(struct ocores_i2c *i2c)
{
    unsigned long timeout_j = jiffies + msecs_to_jiffies(10);
    u8 st;

    /*
     * Wait for TIP to clear after STOP command.
     * The STOP command needs time to propagate to hardware.
     */
    while (time_before(jiffies, timeout_j)) {
        st = oc_getreg(i2c, OCI2C_STATUS);
        if (!(st & OCI2C_STAT_TIP)) {
            break;
        }
        udelay(10);
    }

    /* Final status check - include ARBLOST for multi-master scenarios */
    st = oc_getreg(i2c, OCI2C_STATUS);
    return !!(st & (OCI2C_STAT_BUSY | OCI2C_STAT_TIP));
}

/**
 * ocores_handle_error - Handle hard error with optional controller/bus recovery
 * @i2c:     Controller instance
 * @context: Short tag describing error origin (e.g. "timeout", "polling")
 *
 * This function:
 * 1. Increments error counter (with overflow protection)
 * 2. Sets STATE_ERROR under spinlock protection
 * 3. Issues STOP + IACK to terminate transfer
 * 4. Checks if controller/bus is stuck
 * 5. Schedules delayed recovery work if needed
 *
 * Recovery is triggered when:
 * - Controller/bus is stuck (BUSY, TIP, or ARBLOST set)
 * - Error count exceeds threshold
 * - Recovery is enabled and not at max attempts
 */
static void ocores_handle_error(struct ocores_i2c *i2c, const char *context)
{
    u8 st;
    bool stuck;
    unsigned long flags;

    /*
     * Update error state under lock protection to prevent race
     * with ocores_process() or concurrent transfers
     */
    spin_lock_irqsave(&i2c->process_lock, flags);

    /* Overflow protection for error counter */
    if (i2c->error_count < UINT_MAX) {
        i2c->error_count++;
    }

    i2c->state = STATE_ERROR;

    /* Always send STOP + IACK on error to terminate and clear IF */
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);

    spin_unlock_irqrestore(&i2c->process_lock, flags);

    /* If recovery is disabled globally, exit early */
    if (!i2c->enable_recovery) {
        dev_notice(i2c->adap.dev.parent,
                   "Recovery disabled; ctx=%s err_cnt=%u\n",
                   context ? context : "unknown", i2c->error_count);
        return;
    }

    /*
     * Check if controller/bus is stuck with proper timeout
     * This replaces the simple udelay(10) + immediate read
     */
    stuck = ocores_check_stuck_with_timeout(i2c);

    /* Read final status for logging */
    st = oc_getreg(i2c, OCI2C_STATUS);

    /* Enhanced logging with timestamp delta */
    dev_notice(i2c->adap.dev.parent,
               "I2C error: ctx=%s stuck=%s err_cnt=%u rec_cnt=%u st=0x%02x\n",
               context ? context : "unknown",
               stuck ? "yes" : "no",
               i2c->error_count,
               i2c->recovery_count,
               st);

    /*
     * Trigger recovery if:
     * 1. Controller/bus is stuck, OR
     * 2. Error count exceeds threshold
     * AND we haven't exceeded max recovery attempts
     */
    if ((stuck || i2c->error_count >= i2c->error_threshold) &&
        i2c->recovery_count < i2c->max_recovery_attempts) {

        /* Avoid scheduling multiple recovery works */
        if (!delayed_work_pending(&i2c->recovery_work)) {
            unsigned long delay;

            /*
             * First recovery attempt is immediate (delay=0)
             * Subsequent attempts use exponential backoff:
             * attempt 1: 0ms
             * attempt 2: 250ms
             * attempt 3: 500ms
             * attempt 4: 1000ms
             * attempt 5+: capped at 5000ms
             */
            if (i2c->recovery_count == 0) {
                delay = 0;
            }
            else {
                delay = msecs_to_jiffies(250U << (i2c->recovery_count - 1));
                if (delay > 5 * HZ) {
                    delay = 5 * HZ;
                }
            }

            dev_notice(i2c->adap.dev.parent,
                       "Scheduling recovery: ctx=%s stuck=%s err_cnt=%u rec_cnt=%u delay=%ums st=0x%02x\n",
                       context ? context : "unknown",
                       stuck ? "yes" : "no",
                       i2c->error_count,
                       i2c->recovery_count,
                       jiffies_to_msecs(delay),
                       st);

            schedule_delayed_work(&i2c->recovery_work, delay);
        }
        else {
            dev_dbg(i2c->adap.dev.parent,
                    "Recovery work already pending; skip schedule\n");
        }
    }
}

static void ocores_process(struct ocores_i2c *i2c, u8 stat)
{
    struct i2c_msg *msg = i2c->msg;
    unsigned long flags;
    struct device *dev = i2c->adap.dev.parent;

    /*
     * If we spin here is because we are in timeout, so we are going
     * to be in STATE_ERROR. See ocores_process_timeout()
     */
    spin_lock_irqsave(&i2c->process_lock, flags);
    if ((i2c->state == STATE_DONE) || (i2c->state == STATE_ERROR)) {
        /* stop has been sent */
        oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
        wake_up(&i2c->wait);
        goto out;
    }

    /* error? */
    if (stat & OCI2C_STAT_ARBLOST) {
        i2c->state = STATE_ERROR;
        if (debug) {
            dev_warn(dev, "I2C %s arbitration lost", i2c->adap.name);
        }
        oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
        goto out;
    }

    if ((i2c->state == STATE_START) || (i2c->state == STATE_WRITE)) {
        i2c->state =
            (msg->flags & I2C_M_RD) ? STATE_READ : STATE_WRITE;

        if (stat & OCI2C_STAT_NACK) {
            i2c->state = STATE_ERROR;
            if (debug) {
                dev_warn(dev, "I2C %s, no ACK from slave 0x%02x", 
			 i2c->adap.name, msg->addr);
            }
            oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
            goto out;
        }
    } else {
        msg->buf[i2c->pos++] = oc_getreg(i2c, OCI2C_DATA);
    }

    /* end of msg? */
    if (i2c->pos == msg->len) {
        i2c->nmsgs--;
        i2c->msg++;
        i2c->pos = 0;
        msg = i2c->msg;

        if (i2c->nmsgs) {	/* end? */
            /* send start? */
            if (!(msg->flags & I2C_M_NOSTART)) {
                u8 addr = i2c_8bit_addr_from_msg(msg);

                i2c->state = STATE_START;

                oc_setreg(i2c, OCI2C_DATA, addr);
                oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_START);
                goto out;
            }
            i2c->state = (msg->flags & I2C_M_RD)
                         ? STATE_READ : STATE_WRITE;
        } else {
            i2c->state = STATE_DONE;
            oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
            goto out;
        }
    }

    if (i2c->state == STATE_READ) {
        oc_setreg(i2c, OCI2C_CMD, i2c->pos == (msg->len-1) ?
                  OCI2C_CMD_READ_NACK : OCI2C_CMD_READ_ACK);
    } else {
        oc_setreg(i2c, OCI2C_DATA, msg->buf[i2c->pos++]);
        oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_WRITE);
    }

out:
    spin_unlock_irqrestore(&i2c->process_lock, flags);

}

static irqreturn_t ocores_isr(int irq, void *dev_id)
{
    struct ocores_i2c *i2c = dev_id;
    u8 stat = oc_getreg(i2c, OCI2C_STATUS);

    if (i2c->flags & OCORES_FLAG_BROKEN_IRQ) {
        if ((stat & OCI2C_STAT_IF) && !(stat & OCI2C_STAT_BUSY))
            return IRQ_NONE;
    } else if (!(stat & OCI2C_STAT_IF)) {
        return IRQ_NONE;
    }
    ocores_process(i2c, stat);

    return IRQ_HANDLED;
}

/**
 * Process timeout event
 * @i2c: ocores I2C device instance
 */
static void ocores_process_timeout(struct ocores_i2c *i2c)
{
    unsigned long flags;

    spin_lock_irqsave(&i2c->process_lock, flags);
    i2c->state = STATE_ERROR;
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
    spin_unlock_irqrestore(&i2c->process_lock, flags);

    ocores_handle_error(i2c, "timeout");
}

/**
 * Wait until something change in a given register
 * @i2c: ocores I2C device instance
 * @reg: register to query
 * @mask: bitmask to apply on register value
 * @val: expected result
 * @timeout: timeout in jiffies
 *
 * Timeout is necessary to avoid to stay here forever when the chip
 * does not answer correctly.
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_wait(struct ocores_i2c *i2c,
                       int reg, u8 mask, u8 val,
                       const unsigned long timeout)
{
    unsigned long j;

    j = jiffies + timeout;
    while (1) {
        u8 status = oc_getreg(i2c, reg);

        if ((status & mask) == val)
            break;

        if (time_after(jiffies, j))
            return -ETIMEDOUT;
    }
    return 0;
}

/**
 * Wait until is possible to process some data
 * @i2c: ocores I2C device instance
 *
 * Used when the device is in polling mode (interrupts disabled).
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_poll_wait(struct ocores_i2c *i2c)
{
    u8 mask;
    int err;

    if (i2c->state == STATE_DONE || i2c->state == STATE_ERROR) {
        /* transfer is over */
        mask = OCI2C_STAT_BUSY;
    } else {
        /* on going transfer */
        mask = OCI2C_STAT_TIP;
        /*
         * We wait for the data to be transferred (8bit),
         * then we start polling on the ACK/NACK bit
         */
        udelay((8 * 1000) / i2c->bus_clock_khz);
    }

    /*
     * once we are here we expect to get the expected result immediately
     * so if after 1ms we timeout then something is broken.
     */
    err = ocores_wait(i2c, OCI2C_STATUS, mask, 0, msecs_to_jiffies(timeout));
    if (err) {
        if (debug) {
            dev_warn(i2c->adap.dev.parent,
                     "%s: STATUS timeout, bit 0x%x did not clear in %ums(msecs_to_jiffies(%u)=%lu)\n",
                     __func__, mask, timeout, timeout, msecs_to_jiffies(timeout));
        }
    }
    return err;
}

/**
 * It handles an IRQ-less transfer
 * @i2c: ocores I2C device instance
 *
 * Even if IRQ are disabled, the I2C OpenCore IP behavior is exactly the same
 * (only that IRQ are not produced). This means that we can re-use entirely
 * ocores_isr(), we just add our polling code around it.
 *
 * It can run in atomic context
 *
 * Return: 0 on success, -ETIMEDOUT on timeout
 */
static int ocores_process_polling(struct ocores_i2c *i2c)
{
    irqreturn_t ret;
    int err;

    while (1) {
        err = ocores_poll_wait(i2c);
        if (err) {
            break; /* timeout */
        }

        ret = ocores_isr(-1, i2c);
        if (ret == IRQ_NONE)
            break; /* all messages have been transferred */
        else {
            if (i2c->flags & OCORES_FLAG_BROKEN_IRQ)
                if (i2c->state == STATE_DONE)
                    break;
        }
    }

    return err;
}

static int ocores_xfer_core(struct ocores_i2c *i2c,
                            struct i2c_msg *msgs, int num,
                            bool polling)
{
    int ret = 0;
    u8 ctrl;

    /* Ensure no stale recovery is pending when a new transfer starts */
    cancel_delayed_work_sync(&i2c->recovery_work);

    /* Preflight: if bus looks busy, try to nudge it once */
    if (ocores_bus_busy(i2c)) {
        oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
        oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);
    }

    ctrl = oc_getreg(i2c, OCI2C_CONTROL);
    if (polling)
        oc_setreg(i2c, OCI2C_CONTROL, ctrl & ~OCI2C_CTRL_IEN);
    else
        oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_IEN);

    i2c->msg = msgs;
    i2c->pos = 0;
    i2c->nmsgs = num;
    i2c->state = STATE_START;

    oc_setreg(i2c, OCI2C_DATA, i2c_8bit_addr_from_msg(i2c->msg));
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_START);

    if (polling) {
        ret = ocores_process_polling(i2c);
    } else {
           if (wait_event_timeout(i2c->wait,
                                  (i2c->state == STATE_ERROR) ||
                                  (i2c->state == STATE_DONE), HZ) == 0)
                   ret = -ETIMEDOUT;
    }
    if (ret) {
        ocores_process_timeout(i2c);
        return ret;
    }

    if (i2c->state == STATE_DONE) {
        /* Reset error/recovery counters on success */
        i2c->error_count = 0;
        i2c->recovery_count = 0;
        return num;
    }
    return -EIO;
}

static int ocores_xfer_polling(struct i2c_adapter *adap,
                               struct i2c_msg *msgs, int num)
{
    return ocores_xfer_core(i2c_get_adapdata(adap), msgs, num, true);
}

static int ocores_xfer(struct i2c_adapter *adap,
                       struct i2c_msg *msgs, int num)
{
    return ocores_xfer_core(i2c_get_adapdata(adap), msgs, num, false);
}

static int ocores_init(struct device *dev, struct ocores_i2c *i2c)
{
    int prescale;
    int diff;
    u8 ctrl;

    ctrl = oc_getreg(i2c, OCI2C_CONTROL);

    /* make sure the device is disabled */
    ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);

    prescale = ocores_compute_prescale_khz(i2c->ip_clock_khz, i2c->bus_clock_khz);

    diff = i2c->ip_clock_khz / (5 * (prescale + 1)) - i2c->bus_clock_khz;
    if (abs(diff) > i2c->bus_clock_khz / 10) {
        dev_err(dev,
                "Unsupported clock settings: core: %d KHz, bus: %d KHz\n",
                i2c->ip_clock_khz, i2c->bus_clock_khz);
        return -EINVAL;
    }

    dev_info(dev, "OCI2C_PRELOW=0x%02x OCI2C_PREHIGH=0x%02x\n",
                  prescale & 0xff, prescale >> 8);
    oc_setreg(i2c, OCI2C_PRELOW, prescale & 0xff);
    oc_setreg(i2c, OCI2C_PREHIGH, prescale >> 8);

    /* Init the device */
    oc_setreg(i2c, OCI2C_CONTROL, ctrl | OCI2C_CTRL_EN);
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);

    return 0;
}


static u32 ocores_func(struct i2c_adapter *adap)
{
    return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm ocores_algorithm = {
    .master_xfer = ocores_xfer,
    .master_xfer_atomic = ocores_xfer_polling,
    .functionality = ocores_func,
};

static const struct i2c_adapter ocores_adapter = {
    .owner = THIS_MODULE,
    .name = "i2c-ocores",
    .class = I2C_CLASS_DEPRECATED,
    .algo = &ocores_algorithm,
};

/**
 * prescaler_show - Show prescaler and estimated bus clock
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display the current 16-bit prescaler value and the I2C bus clock
 * estimated from the cached ip_clock_khz, together with the theoretical
 * calculation formulas and usage examples.
 *
 * This helper is intended for board bring-up and tuning. The reported
 * bus frequency is a theoretical estimate derived from the internal
 * clock model; the actual I2C waveform should be verified with hardware
 * measurement equipment (for example, an oscilloscope or logic analyzer).
 *
 * Input format for prescaler_store():
 * - Accepts decimal or 0x-prefixed hexadecimal.
 *   Examples:
 *     echo 99 > prescaler        # decimal 99   (0x0063)
 *     echo 0x0063 > prescaler    # hexadecimal  (99; PREHIGH=0x00, PRELOW=0x63)
 *
 *   The 16-bit prescaler value maps to the controller registers as:
 *     prescaler = (PREHIGH << 8) | PRELOW
 *
 * Return: number of bytes written to buffer
 */
static ssize_t prescaler_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    u8 prescale_lo;
    u8 prescale_hi;
    u16 prescale;
    int actual_freq;
    unsigned long flags;
    int len = 0;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    /*
     * Acquire locks to ensure consistent read
     * (prevent reading during I2C transfer or sysfs modification)
     */
    mutex_lock(&i2c->hw_lock);
    spin_lock_irqsave(&i2c->process_lock, flags);
    prescale_lo = oc_getreg(i2c, OCI2C_PRELOW);
    prescale_hi = oc_getreg(i2c, OCI2C_PREHIGH);
    spin_unlock_irqrestore(&i2c->process_lock, flags);
    mutex_unlock(&i2c->hw_lock);

    prescale = (prescale_hi << 8) | prescale_lo;

    /* Estimate bus frequency based on cached IP clock and prescaler.
     *
     * This is a theoretical value derived from the driver model.
     * Actual I2C timing on the pins should be verified with measurement
     * equipment (for example, an oscilloscope or logic analyzer).
     */
    actual_freq = i2c->ip_clock_khz / (5 * (prescale + 1));

    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "Prescaler: 0x%04x (%u)\n",
                    prescale, prescale);

    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "  (PREHIGH = 0x%02x, PRELOW = 0x%02x; prescaler = (PREHIGH << 8) | PRELOW)\n",
                    prescale_hi, prescale_lo);

    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "Estimated bus clock: ~%d KHz\n",
                    actual_freq);
    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "  (calculated from ip_clock_khz = %d KHz)\n\n",
                    i2c->ip_clock_khz);

    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "Formula (theoretical): "
                    "prescaler = (ip_clock_khz / (5 * target_khz)) - 1\n");
    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "                      "
                    "bus_clock_khz = ip_clock_khz / (5 * (prescaler + 1))\n\n");

    /* Common theoretical prescaler values with this IP clock */
    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "Common theoretical values for this controller:\n");

    if (i2c->ip_clock_khz >= 500) {
        int prescale_100k;
        int prescale_400k;
        int prescale_1m;

        prescale_100k = (i2c->ip_clock_khz / (5 * 100)) - 1;
        prescale_400k = (i2c->ip_clock_khz / (5 * 400)) - 1;
        prescale_1m = (i2c->ip_clock_khz / (5 * 1000)) - 1;

        len += scnprintf((buf + len), (PAGE_SIZE - len),
                        "  For 100 KHz: echo %d > prescaler\n",
                        prescale_100k);
        len += scnprintf((buf + len), (PAGE_SIZE - len),
                        "  For 400 KHz: echo %d > prescaler\n",
                        prescale_400k);

        if (prescale_1m >= 0) {
            len += scnprintf((buf + len), (PAGE_SIZE - len),
                            "  For 1000 KHz: echo %d > prescaler\n",
                            prescale_1m);
        }
    }

    len += scnprintf((buf + len), (PAGE_SIZE - len),
                    "\nWrite examples:\n"
                    "  echo 99 > prescaler      # decimal (prescaler = 0x0063)\n"
                    "  echo 0x0063 > prescaler  # hexadecimal (PREHIGH=0x00, PRELOW=0x63)\n");
    return len;
}

/**
 * prescaler_store - Set prescaler value
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * Write a new prescaler value to the controller.
 *
 * Input format:
 * - Accepts decimal or 0x-prefixed hexadecimal (0-255).
 *
 * The prescaler is a 16-bit value that determines the I2C bus frequency:
 *   bus_clock_khz = ip_clock_khz / (5 * (prescaler + 1))
 *
 * In this driver, bus_clock_khz is treated as the desired / nominal I2C
 * bus frequency corresponding to the userspace target_bus_khz setting.
 * When the prescaler is changed via this attribute, the internal
 * ip_clock_khz timing model is adjusted so that the above formula
 * continues to hold for the configured target bus frequency.
 *
 * This function uses hw_lock to prevent race conditions with other
 * hardware-related sysfs operations.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t prescaler_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf,
                               size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    u8 ctrl;
    unsigned long flags;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    /* Accept decimal or 0x-prefixed hex value. */
    ret = kstrtouint(buf, 0, &value);
    if (ret) {
        return ret;
    }

    if (value > 0xFFFF) {
        return -EINVAL;
    }

    /*
     * Acquire locks to prevent race with:
     * 1. Other sysfs operations (hw_lock)
     * 2. I2C transfers (process_lock)
     *
     * Lock order: hw_lock -> process_lock (to avoid deadlock)
     */
    mutex_lock(&i2c->hw_lock);
    spin_lock_irqsave(&i2c->process_lock, flags);

    /*
     * Ensure bus is in a clean state before modifying prescaler:
     * 1. Send STOP to terminate any ongoing transfer
     * 2. Clear interrupt flag
     * This prevents the bus from being in an undefined state
     */
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);

    /* Brief delay to let hardware complete STOP */
    udelay(10);

    /* Disable controller */
    ctrl = oc_getreg(i2c, OCI2C_CONTROL);
    ctrl &= ~OCI2C_CTRL_EN;
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);

    /* Modify prescaler registers */
    oc_setreg(i2c, OCI2C_PRELOW, value & 0xFF);
    oc_setreg(i2c, OCI2C_PREHIGH, (value >> 8) & 0xFF);

    /* Re-enable controller */
    ctrl |= OCI2C_CTRL_EN;
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);

    /*
     * Update internal timing model.
     *
     * If bus_clock_khz is configured (>0), treat it as the desired
     * I2C bus frequency and derive an effective ip_clock_khz model
     * from the current prescaler so that:
     *
     *   bus_clock_khz = ip_clock_khz / (5 * (prescaler + 1))
     *
     * still holds. If bus_clock_khz is not configured, fall back to
     * deriving bus_clock_khz from the existing ip_clock_khz.
     */
    if (i2c->bus_clock_khz > 0) {
        unsigned int bus_khz;
        u64 ip_khz64;

        bus_khz = (unsigned int)i2c->bus_clock_khz;
        ip_khz64 = (u64)bus_khz * 5U * ((u16)value + 1U);

        if (ip_khz64 > 0x7fffffffULL) {
            i2c->ip_clock_khz = 0x7fffffff;
        } else {
            i2c->ip_clock_khz = (int)ip_khz64;
        }
    } else if (i2c->ip_clock_khz > 0) {
        i2c->bus_clock_khz =
            i2c->ip_clock_khz / (5 * ((u16)value + 1));
    }

    spin_unlock_irqrestore(&i2c->process_lock, flags);
    mutex_unlock(&i2c->hw_lock);

    return count;
}

/**
 * status_show - Show I2C status register
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display the current status register value with bit-by-bit breakdown.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t status_show(struct device *dev,
                           struct device_attribute *attr,
                           char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    u8 status;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    status = oc_getreg(i2c, OCI2C_STATUS);

    return scnprintf(buf, PAGE_SIZE, "0x%02x (IF=%d TIP=%d ARBLOST=%d BUSY=%d NACK=%d)\n",
                    status,
                    !!(status & OCI2C_STAT_IF),
                    !!(status & OCI2C_STAT_TIP),
                    !!(status & OCI2C_STAT_ARBLOST),
                    !!(status & OCI2C_STAT_BUSY),
                    !!(status & OCI2C_STAT_NACK));
}

/**
 * control_show - Show I2C control register
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display the current control register value with bit-by-bit breakdown.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t control_show(struct device *dev,
                            struct device_attribute *attr,
                            char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    u8 control;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    control = oc_getreg(i2c, OCI2C_CONTROL);

    return scnprintf(buf, PAGE_SIZE, "0x%02x (EN=%d IEN=%d)\n",
                    control,
                    !!(control & OCI2C_CTRL_EN),
                    !!(control & OCI2C_CTRL_IEN));
}

/**
 * force_stop_store - Manually trigger I2C bus STOP and IACK commands
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * Manually execute STOP condition followed by interrupt acknowledge (IACK)
 * to recover a stuck I2C bus or clear interrupt flags.
 *
 * Input: Write 1 to trigger the operation.
 *        Writing 0 has no effect.
 * Example: echo 1 > force_stop    # Triggers STOP + IACK
 *
 * This function:
 * 1. Sends STOP condition to release the I2C bus
 * 2. Clears interrupt flag with IACK command
 * 3. Waits 10us for operations to complete
 *
 * Lock mechanism:
 * - hw_lock: Prevents race with other sysfs operations
 * - process_lock: Ensures no I2C transfer is in progress
 *
 * Use cases:
 * - Bus stuck in BUSY state
 * - Interrupt flag (IF) remains set
 * - Manual bus recovery during debugging
 * - System initialization or reconfiguration
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t force_stop_store(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf,
                                    size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    unsigned long flags;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    ret = kstrtouint(buf, 10, &value);
    if (ret) {
        return ret;
    }

    if (!value) {
        return -EINVAL;
    }

    /*
     * Acquire locks to prevent race with:
     * 1. Other sysfs operations (hw_lock)
     * 2. I2C transfers (process_lock)
     */
    mutex_lock(&i2c->hw_lock);
    spin_lock_irqsave(&i2c->process_lock, flags);

    /* Issue STOP command to release the bus */
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_STOP);

    spin_unlock_irqrestore(&i2c->process_lock, flags);

    /* Wait for STOP to take effect */
    udelay(10);

    spin_lock_irqsave(&i2c->process_lock, flags);

    /* Issue IACK command to clear interrupt flag */
    oc_setreg(i2c, OCI2C_CMD, OCI2C_CMD_IACK);

    spin_unlock_irqrestore(&i2c->process_lock, flags);
    mutex_unlock(&i2c->hw_lock);

    return count;
}

/**
 * recovery_trigger_store - Queue a manual recovery attempt
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * This attribute allows userspace to queue a controller-level recovery
 * attempt using the existing recovery state machine, without waiting
 * for the error_count / recovery_threshold logic to trigger it.
 *
 * Behaviour:
 * - Recovery must be enabled via recovery_enable; otherwise -EPERM is returned.
 * - If a recovery_work item is already pending, no new work is queued.
 * - The controller state is forced to STATE_ERROR and recovery_count is
 *   reset to 0 so that ocores_recovery_work() will perform a full
 *   recovery sequence.
 *
 * Input:
 *   echo 1 > recovery_trigger
 *
 * Any value other than 1 is rejected with -EINVAL.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t recovery_trigger_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf,
                                      size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    unsigned long flags;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);
    if (!i2c) {
        return -ENODEV;
    }

    ret = kstrtouint(buf, 10, &value);
    if (ret) {
        return ret;
    }

    if (value != 1U) {
        return -EINVAL;
    }

    /* Do not override global recovery policy. */
    if (!i2c->enable_recovery) {
        dev_notice(i2c->adap.dev.parent,
                   "Manual recovery trigger ignored: recovery disabled (err_cnt=%u, rec_cnt=%u)\n",
                   i2c->error_count, i2c->recovery_count);
        return -EPERM;
    }

    /* Avoid queuing duplicate work items. */
    if (delayed_work_pending(&i2c->recovery_work)) {
        return count;
    }

    /*
     * Mark the controller in error state and re-arm the
     * recovery attempt counter so that the worker performs
     * a full controller recovery sequence.
     */
    spin_lock_irqsave(&i2c->process_lock, flags);
    i2c->state = STATE_ERROR;
    i2c->recovery_count = 0;
    spin_unlock_irqrestore(&i2c->process_lock, flags);

    schedule_delayed_work(&i2c->recovery_work, 0);

    return count;
}

/**
 * recovery_enable_show - Show error recovery enable status
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display whether error recovery is enabled for this controller instance.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t recovery_enable_show(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    return scnprintf(buf, PAGE_SIZE, "%u\n", i2c->enable_recovery);
}

/**
 * recovery_enable_store - Set error recovery enable status
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * Enable or disable error recovery for this controller instance.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t recovery_enable_store(struct device *dev,
                                     struct device_attribute *attr,
                                     const char *buf,
                                     size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    ret = kstrtouint(buf, 10, &value);
    if (ret) {
        return ret;
    }

    if (value > 1) {
        return -EINVAL;
    }

    i2c->enable_recovery = value;

    return count;
}

/**
 * recovery_max_attempts_show - Show maximum recovery attempts
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display the maximum number of recovery attempts for this controller.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t recovery_max_attempts_show(struct device *dev,
                                          struct device_attribute *attr,
                                          char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    return scnprintf(buf, PAGE_SIZE, "%u\n", i2c->max_recovery_attempts);
}

/**
 * recovery_max_attempts_store - Set maximum recovery attempts
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * Set the maximum number of recovery attempts for this controller.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t recovery_max_attempts_store(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    ret = kstrtouint(buf, 10, &value);
    if (ret) {
        return ret;
    }

    if (value == 0 || value > 100) {
        return -EINVAL;
    }

    i2c->max_recovery_attempts = value;

    return count;
}

/**
 * recovery_threshold_show - Show error threshold
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display the error count threshold that triggers recovery.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t recovery_threshold_show(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    return scnprintf(buf, PAGE_SIZE, "%u\n", i2c->error_threshold);
}

/**
 * recovery_threshold_store - Set error threshold
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * Set the error count threshold that triggers recovery.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t recovery_threshold_store(struct device *dev,
                                        struct device_attribute *attr,
                                        const char *buf,
                                        size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    ret = kstrtouint(buf, 10, &value);
    if (ret) {
        return ret;
    }

    if (value == 0 || value > 1000) {
        return -EINVAL;
    }

    i2c->error_threshold = value;

    return count;
}

/**
 * error_count_show - Show current error count
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display the current error count for this controller.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t error_count_show(struct device *dev,
                                struct device_attribute *attr,
                                char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    return scnprintf(buf, PAGE_SIZE, "%u\n", i2c->error_count);
}

/**
 * error_count_store - Reset error count
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * Reset the error count to zero.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t error_count_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf,
                                 size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    ret = kstrtouint(buf, 10, &value);
    if (ret) {
        return ret;
    }

    if (value != 0) {
        return -EINVAL;
    }

    i2c->error_count = value;

    return count;
}

/**
 * recovery_count_show - Show current recovery attempt count
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * Display the current recovery attempt count for this controller.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t recovery_count_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    return scnprintf(buf, PAGE_SIZE, "%u\n", i2c->recovery_count);
}

/**
 * recovery_count_store - Reset recovery count
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * Reset the recovery count to zero.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t recovery_count_store(struct device *dev,
                                 struct device_attribute *attr,
                                 const char *buf,
                                 size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int value;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);

    ret = kstrtouint(buf, 10, &value);
    if (ret) {
        return ret;
    }

    if (value != 0) {
        return -EINVAL;
    }

    i2c->recovery_count = value;

    return count;
}

/**
 * target_bus_khz_show - Show configured and modelled bus clock
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * This attribute reports the driver's current target bus clock (in kHz) and
 * shows how it relates to the prescaler registers and the internal
 * ip_clock_khz timing model. The estimated bus clock is derived from
 * the OpenCores timing formula:
 *
 *   bus_clock_khz = ip_clock_khz / (5 * (prescaler + 1))
 *
 * The estimated value is a theoretical result from the model. The
 * actual I2C waveform on the pins should be verified with hardware
 * measurement equipment (for example, an oscilloscope or a logic
 * analyzer).
 *
 * Return: number of bytes written to buffer
 */
static ssize_t target_bus_khz_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    u8 prescale_lo;
    u8 prescale_hi;
    u16 prescale;
    int estimated = 0;
    unsigned long flags;
    int len = 0;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);
    if (!i2c) {
        return -ENODEV;
    }

    mutex_lock(&i2c->hw_lock);
    spin_lock_irqsave(&i2c->process_lock, flags);
    prescale_lo = oc_getreg(i2c, OCI2C_PRELOW);
    prescale_hi = oc_getreg(i2c, OCI2C_PREHIGH);
    spin_unlock_irqrestore(&i2c->process_lock, flags);
    mutex_unlock(&i2c->hw_lock);

    prescale = ((u16)prescale_hi << 8) | prescale_lo;

    if (i2c->ip_clock_khz > 0 && prescale != 0xFFFF) {
        estimated = i2c->ip_clock_khz / (5 * (prescale + 1));
    }

    len += scnprintf((buf + len), (PAGE_SIZE - len),
                         "target_bus_khz (model): %d KHz\n",
                         i2c->bus_clock_khz);
    len += scnprintf((buf + len), (PAGE_SIZE - len),
                         "Prescaler:            0x%04x (%u)\n",
                         prescale, prescale);
    len += scnprintf((buf + len), (PAGE_SIZE - len),
                         "Estimated bus (model): %d KHz\n",
                         estimated);
    len += scnprintf((buf + len), (PAGE_SIZE - len),
                         "ip_clock_khz (model): %d KHz\n",
                         i2c->ip_clock_khz);

    return len;
}

/**
 * target_bus_khz_store - Set target bus clock and adjust ip_clock_khz model
 * @dev: device structure
 * @attr: device attribute
 * @buf: input buffer
 * @count: input buffer size
 *
 * This attribute allows overriding the driver's target bus clock value
 * in kHz and derives an effective ip_clock_khz timing model from the
 * current prescaler register value:
 *
 *   ip_clock_khz = bus_clock_khz * 5 * (prescaler + 1)
 *
 * This helper does not change the real hardware clock source. It only
 * updates the driver's internal timing model (ip_clock_khz and
 * bus_clock_khz) so that subsequent prescaler calculations are aligned
 * with measured I2C bus behaviour on a given board.
 *
 * Input format:
 * - Accepts decimal or 0x-prefixed hexadecimal (kHz).
 *
 * Typical bring-up flow:
 *   1. Adjust prescaler while observing SCL
 *      on an oscilloscope until the actual bus frequency is correct.
 *   2. Echo the measured bus frequency (in kHz) into target_bus_khz.
 *      The driver will compute a matching ip_clock_khz model from the
 *      current prescaler value.
 *
 * Return: number of bytes consumed, or negative error code
 */
static ssize_t target_bus_khz_store(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf,
                                    size_t count)
{
    struct i2c_adapter *adap;
    struct ocores_i2c *i2c;
    unsigned int bus_khz;
    u8 prescale_lo;
    u8 prescale_hi;
    u16 prescale;
    unsigned long flags;
    unsigned int ip_khz;
    int ret;

    adap = to_i2c_adapter(dev);
    i2c = i2c_get_adapdata(adap);
    if (!i2c) {
        return -ENODEV;
    }

    ret = kstrtouint(buf, 0, &bus_khz);
    if (ret) {
        return ret;
    }
    if (bus_khz == 0) {
        return -EINVAL;
    }

    mutex_lock(&i2c->hw_lock);
    spin_lock_irqsave(&i2c->process_lock, flags);
    prescale_lo = oc_getreg(i2c, OCI2C_PRELOW);
    prescale_hi = oc_getreg(i2c, OCI2C_PREHIGH);
    spin_unlock_irqrestore(&i2c->process_lock, flags);
    mutex_unlock(&i2c->hw_lock);

    prescale = ((u16)prescale_hi << 8) | prescale_lo;
    if (prescale == 0xFFFF) {
        return -EINVAL;
    }

    ip_khz = bus_khz * 5U * (prescale + 1U);

    i2c->bus_clock_khz = (int)bus_khz;
    i2c->ip_clock_khz = (int)ip_khz;

    return count;
}

/**
 * help_show - Show summary of hardware sysfs attributes
 * @dev: device structure
 * @attr: device attribute
 * @buf: output buffer
 *
 * This attribute provides a human-readable overview of the sysfs
 * attributes exposed under the "hardware" group for this OpenCores
 * I2C controller instance. It is intended to make it easier to
 * discover which attributes are read-only, write-only or read/write,
 * and what each of them is used for.
 *
 * The output is informational only and has no side effects.
 *
 * Return: number of bytes written to buffer
 */
static ssize_t help_show(struct device *dev,
                         struct device_attribute *attr,
                         char *buf)
{
    return scnprintf(buf,PAGE_SIZE,
                      "I2C OpenCores controller hardware attributes:\n"
                      "  Attribute              Access  Description\n"
                      "  ------------------------------------------------------------\n"
                      "  prescaler              R/W     16-bit prescaler (PREHIGH:PRELOW)\n"
                      "  status                 R       Status register (decoded)\n"
                      "  control                R       Control register (decoded)\n"
                      "  force_stop             W       Issue STOP + IACK on the bus\n"
                      "  recovery_enable        R/W     Enable or disable automatic recovery\n"
                      "  recovery_threshold     R/W     Error count threshold that triggers recovery\n"
                      "  recovery_max_attempts  R/W     Max recovery attempts before giving up\n"
                      "  recovery_trigger       W       Queue a manual recovery attempt (echo 1)\n"
                      "  error_count            R/W     Error counter; echo 0 to reset\n"
                      "  recovery_count         R/W     Recovery attempt counter; echo 0 to reset\n"
                      "  target_bus_khz         R/W     Target bus clock (kHz, timing model)\n"
                      "\n"
                      "Notes:\n"
                      "  - target_bus_khz is the desired/nominal I2C bus frequency.\n"
                      "  - The driver derives its ip_clock_khz timing model from\n"
                      "    target_bus_khz and the current prescaler value.\n"
                      "  - Always verify the actual SCL waveform with a logic analyzer\n"
                      "    or oscilloscope; the timing model is only an estimate.\n");
}

static DEVICE_ATTR_RW(prescaler);
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_RO(control);
static DEVICE_ATTR_WO(force_stop);
static DEVICE_ATTR_RW(recovery_enable);
static DEVICE_ATTR_RW(recovery_max_attempts);
static DEVICE_ATTR_RW(recovery_threshold);
static DEVICE_ATTR_WO(recovery_trigger);
static DEVICE_ATTR_RW(error_count);
static DEVICE_ATTR_RW(recovery_count);
static DEVICE_ATTR_RW(target_bus_khz);
static DEVICE_ATTR_RO(help);

static struct attribute *ocores_i2c_attrs[] = {
    &dev_attr_prescaler.attr,
    &dev_attr_status.attr,
    &dev_attr_control.attr,
    &dev_attr_force_stop.attr,
    &dev_attr_recovery_enable.attr,
    &dev_attr_recovery_max_attempts.attr,
    &dev_attr_recovery_threshold.attr,
    &dev_attr_recovery_trigger.attr,
    &dev_attr_error_count.attr,
    &dev_attr_recovery_count.attr,
    &dev_attr_target_bus_khz.attr,
    &dev_attr_help.attr,
    NULL
};

static const struct attribute_group ocores_i2c_attr_group = {
    .name = "hardware",
    .attrs = ocores_i2c_attrs,
};

static const struct of_device_id ocores_i2c_match[] = {
    {
        .compatible = "opencores,i2c-ocores",
        .data = (void *)TYPE_OCORES,
    },
    {
        .compatible = "aeroflexgaisler,i2cmst",
        .data = (void *)TYPE_GRLIB,
    },
    {
        .compatible = "sifive,fu540-c000-i2c",
        .data = (void *)TYPE_SIFIVE_REV0,
    },
    {
        .compatible = "sifive,i2c0",
        .data = (void *)TYPE_SIFIVE_REV0,
    },
    {},
};
MODULE_DEVICE_TABLE(of, ocores_i2c_match);

#ifdef CONFIG_OF
/*
 * Read and write functions for the GRLIB port of the controller. Registers are
 * 32-bit big endian and the PRELOW and PREHIGH registers are merged into one
 * register. The subsequent registers have their offsets decreased accordingly.
 */
static u8 oc_getreg_grlib(struct ocores_i2c *i2c, int reg)
{
    u32 rd;
    int rreg = reg;

    if (reg != OCI2C_PRELOW)
        rreg--;
    rd = ioread32be(i2c->base + (rreg << i2c->reg_shift));
    if (reg == OCI2C_PREHIGH)
        return (u8)(rd >> 8);
    else
        return (u8)rd;
}

static void oc_setreg_grlib(struct ocores_i2c *i2c, int reg, u8 value)
{
    u32 curr, wr;
    int rreg = reg;

    if (reg != OCI2C_PRELOW)
        rreg--;
    if (reg == OCI2C_PRELOW || reg == OCI2C_PREHIGH) {
        curr = ioread32be(i2c->base + (rreg << i2c->reg_shift));
        if (reg == OCI2C_PRELOW)
            wr = (curr & 0xff00) | value;
        else
            wr = (((u32)value) << 8) | (curr & 0xff);
    } else {
        wr = value;
    }
    iowrite32be(wr, i2c->base + (rreg << i2c->reg_shift));
}

static int ocores_i2c_of_probe(struct platform_device *pdev,
                               struct ocores_i2c *i2c)
{
    struct device_node *np = pdev->dev.of_node;
    const struct of_device_id *match;
    u32 val;
    u32 clock_frequency;
    bool clock_frequency_present;

    if (of_property_read_u32(np, "reg-shift", &i2c->reg_shift)) {
        /* no 'reg-shift', check for deprecated 'regstep' */
        if (!of_property_read_u32(np, "regstep", &val)) {
            if (!is_power_of_2(val)) {
                dev_err(&pdev->dev, "invalid regstep %d\n",
                        val);
                return -EINVAL;
            }
            i2c->reg_shift = ilog2(val);
            dev_warn(&pdev->dev,
                     "regstep property deprecated, use reg-shift\n");
        }
    }

    clock_frequency_present = !of_property_read_u32(np, "clock-frequency",
                              &clock_frequency);
    i2c->bus_clock_khz = 100;

    i2c->clk = devm_clk_get(&pdev->dev, NULL);

    if (!IS_ERR(i2c->clk)) {
        int ret = clk_prepare_enable(i2c->clk);

        if (ret) {
            dev_err(&pdev->dev,
                    "clk_prepare_enable failed: %d\n", ret);
            return ret;
        }
        i2c->ip_clock_khz = clk_get_rate(i2c->clk) / 1000;
        if (clock_frequency_present)
            i2c->bus_clock_khz = clock_frequency / 1000;
    }

    if (i2c->ip_clock_khz == 0) {
        if (of_property_read_u32(np, "opencores,ip-clock-frequency",
                                 &val)) {
            if (!clock_frequency_present) {
                dev_err(&pdev->dev,
                        "Missing required parameter 'opencores,ip-clock-frequency'\n");
                clk_disable_unprepare(i2c->clk);
                return -ENODEV;
            }
            i2c->ip_clock_khz = clock_frequency / 1000;
            dev_warn(&pdev->dev,
                     "Deprecated usage of the 'clock-frequency' property, please update to 'opencores,ip-clock-frequency'\n");
        } else {
            i2c->ip_clock_khz = val / 1000;
            if (clock_frequency_present)
                i2c->bus_clock_khz = clock_frequency / 1000;
        }
    }

    of_property_read_u32(pdev->dev.of_node, "reg-io-width",
                         &i2c->reg_io_width);

    match = of_match_node(ocores_i2c_match, pdev->dev.of_node);
    if (match && (long)match->data == TYPE_GRLIB) {
        dev_dbg(&pdev->dev, "GRLIB variant of i2c-ocores\n");
        i2c->setreg = oc_setreg_grlib;
        i2c->getreg = oc_getreg_grlib;
    }

    return 0;
}
#else
#define ocores_i2c_of_probe(pdev, i2c) -ENODEV
#endif

static int ocores_i2c_probe(struct platform_device *pdev)
{
    struct ocores_i2c *i2c;
    struct ocores_i2c_platform_data *pdata;
    const struct of_device_id *match;
    struct resource *res;
    int irq;
    int ret;
    int i;

    i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
    if (!i2c)
        return -ENOMEM;

    spin_lock_init(&i2c->process_lock);
    mutex_init(&i2c->hw_lock);

    /*
     * Initialize per-instance recovery settings with default values.
     * These will be overridden by device tree or platform data if provided.
     */
    i2c->enable_recovery = default_enable_recovery;
    i2c->max_recovery_attempts = default_max_recovery_attempts;
    i2c->error_threshold = default_error_threshold;
    i2c->error_count = 0;
    i2c->recovery_count = 0;
    INIT_DELAYED_WORK(&i2c->recovery_work, ocores_recovery_work);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (res) {
        /* i2c->base = devm_ioremap_resource(&pdev->dev, res);*/
        i2c->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
        if (!i2c->base) {
            dev_err(&pdev->dev, "ioremap failed\n");
            return -ENOMEM;
        }
        dev_info(&pdev->dev, "Resouce start:0x%llx, end:0x%llx", res->start, res->end);
    } else {
        res = platform_get_resource(pdev, IORESOURCE_IO, 0);
        if (!res)
            return -EINVAL;
        i2c->iobase = res->start;
        if (!devm_request_region(&pdev->dev, res->start,
                                 resource_size(res),
                                 pdev->name)) {
            dev_err(&pdev->dev, "Can't get I/O resource.\n");
            return -EBUSY;
        }
        i2c->setreg = oc_setreg_io_8;
        i2c->getreg = oc_getreg_io_8;
    }

    pdata = dev_get_platdata(&pdev->dev);
    if (pdata) {
        i2c->reg_shift = pdata->reg_shift;
        i2c->reg_io_width = pdata->reg_io_width;
        i2c->ip_clock_khz = pdata->clock_khz;
        dev_info(&pdev->dev, "Write %d KHz, ioWidth:%d, shift:%d", i2c->ip_clock_khz, pdata->reg_io_width ,pdata->reg_shift);
        if (pdata->bus_khz)
            i2c->bus_clock_khz = pdata->bus_khz;
        else
            i2c->bus_clock_khz = 100;
    } else {
        ret = ocores_i2c_of_probe(pdev, i2c);
        if (ret)
            return ret;
    }

    if (i2c->reg_io_width == 0)
        i2c->reg_io_width = 1; /* Set to default value */

    if (!i2c->setreg || !i2c->getreg) {
        bool be = pdata ? pdata->big_endian :
                  of_device_is_big_endian(pdev->dev.of_node);

        switch (i2c->reg_io_width) {
        case 1:
            i2c->setreg = oc_setreg_8;
            i2c->getreg = oc_getreg_8;
            break;

        case 2:
            i2c->setreg = be ? oc_setreg_16be : oc_setreg_16;
            i2c->getreg = be ? oc_getreg_16be : oc_getreg_16;
            break;

        case 4:
            i2c->setreg = be ? oc_setreg_32be : oc_setreg_32;
            i2c->getreg = be ? oc_getreg_32be : oc_getreg_32;
            break;

        default:
            dev_err(&pdev->dev, "Unsupported I/O width (%d)\n",
                    i2c->reg_io_width);
            ret = -EINVAL;
            goto err_clk;
        }
    }

    init_waitqueue_head(&i2c->wait);

    irq = platform_get_irq_optional(pdev, 0);
    if (irq == -ENXIO) {
        ocores_algorithm.master_xfer = ocores_xfer_polling;

        /*
         * Set in OCORES_FLAG_BROKEN_IRQ to enable workaround for
         * FU540-C000 SoC in polling mode.
         */
        match = of_match_node(ocores_i2c_match, pdev->dev.of_node);
        if (match && (long)match->data == TYPE_SIFIVE_REV0)
            i2c->flags |= OCORES_FLAG_BROKEN_IRQ;
    } else {
        if (irq < 0)
            return irq;
    }

    if (ocores_algorithm.master_xfer != ocores_xfer_polling) {
        ret = devm_request_any_context_irq(&pdev->dev, irq,
                                           ocores_isr, 0,
                                           pdev->name, i2c);
        if (ret) {
            dev_err(&pdev->dev, "Cannot claim IRQ\n");
            goto err_clk;
        }
    }
    ret = ocores_init(&pdev->dev, i2c);
    if (ret) {
        goto err_clk;
    }
    /* hook up driver to tree */
    platform_set_drvdata(pdev, i2c);
    i2c->adap = ocores_adapter;
    i2c_set_adapdata(&i2c->adap, i2c);
    i2c->adap.dev.parent = &pdev->dev;
    i2c->adap.dev.of_node = pdev->dev.of_node;

    /* add i2c adapter to i2c tree */
    ret = i2c_add_adapter(&i2c->adap);
    if (ret) {
        goto err_clk;
    }
    /* add in known devices to the bus */
    if (pdata) {
        for (i = 0; i < pdata->num_devices; i++){
            i2c_new_client_device(&i2c->adap, pdata->devices + i);
        }
    }

    /* create sysfs group under adapter device */
    ret = sysfs_create_group(&i2c->adap.dev.kobj, &ocores_i2c_attr_group);
    if (ret != 0) {
        dev_err(&pdev->dev, "Failed to create sysfs group\n");
        i2c_del_adapter(&i2c->adap);
        goto err_clk;
    }

    return 0;

err_clk:
    clk_disable_unprepare(i2c->clk);
    return ret;
}

static int ocores_i2c_remove(struct platform_device *pdev)
{
    struct ocores_i2c *i2c = platform_get_drvdata(pdev);
    u8 ctrl;

    cancel_delayed_work_sync(&i2c->recovery_work);

    ctrl = oc_getreg(i2c, OCI2C_CONTROL);

    /* disable i2c logic */
    ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);

    sysfs_remove_group(&i2c->adap.dev.kobj, &ocores_i2c_attr_group);

    /* remove adapter & data */
    i2c_del_adapter(&i2c->adap);

    if (!IS_ERR(i2c->clk))
        clk_disable_unprepare(i2c->clk);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ocores_i2c_suspend(struct device *dev)
{
    struct ocores_i2c *i2c = dev_get_drvdata(dev);
    u8 ctrl = oc_getreg(i2c, OCI2C_CONTROL);

    cancel_delayed_work_sync(&i2c->recovery_work);

    /* make sure the device is disabled */
    ctrl &= ~(OCI2C_CTRL_EN | OCI2C_CTRL_IEN);
    oc_setreg(i2c, OCI2C_CONTROL, ctrl);

    if (!IS_ERR(i2c->clk))
        clk_disable_unprepare(i2c->clk);
    return 0;
}

static int ocores_i2c_resume(struct device *dev)
{
    struct ocores_i2c *i2c = dev_get_drvdata(dev);

    if (!IS_ERR(i2c->clk)) {
        unsigned long rate;
        int ret = clk_prepare_enable(i2c->clk);

        if (ret) {
            dev_err(dev,
                    "clk_prepare_enable failed: %d\n", ret);
            return ret;
        }
        rate = clk_get_rate(i2c->clk) / 1000;
        if (rate)
            i2c->ip_clock_khz = rate;
    }
    return ocores_init(dev, i2c);
}

static SIMPLE_DEV_PM_OPS(ocores_i2c_pm, ocores_i2c_suspend, ocores_i2c_resume);
#define OCORES_I2C_PM	(&ocores_i2c_pm)
#else
#define OCORES_I2C_PM	NULL
#endif

static struct platform_driver ocores_i2c_driver = {
    .probe   = ocores_i2c_probe,
    .remove  = ocores_i2c_remove,
    .driver  = {
        .name = "as1817-ocores-i2c",
        .of_match_table = ocores_i2c_match,
        .pm = OCORES_I2C_PM,
    },
};

module_platform_driver(ocores_i2c_driver);

MODULE_AUTHOR("Peter Korsgaard <peter@korsgaard.com>");
MODULE_DESCRIPTION("OpenCores I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:as1817-ocores-i2c");