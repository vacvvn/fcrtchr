#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
//  #include <linux/scheD.h> // Jiffies defines in this header file
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>

#define	HRTIMER0_US	(100 * 1000 * 1000)
 
static struct hrtimer g_hrtimer0;
static u64 nsec_time = HRTIMER0_US;
 
 
static enum hrtimer_restart hrtimer_test_fn(struct hrtimer *hrtimer)
{
	static u64 divider =0;
	divider++;
	if(divider == 10)
	{
		pr_err("#### hrtimer timeout: 1Sec");
		divider = 0;
	}
	
    hrtimer_forward_now(hrtimer, ns_to_ktime(nsec_time));
	return HRTIMER_RESTART;
}
 
int rtsk_init_task(u64 nsec_period)
{
	pr_err("#### hr_timer_test module init...\n");
	
	nsec_time = nsec_period;
	struct hrtimer *hrtimer = &g_hrtimer0;
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	hrtimer->function = hrtimer_test_fn;
	hrtimer_start(hrtimer, ns_to_ktime(nsec_period),
		      HRTIMER_MODE_REL_PINNED);
    
	return 0;
}
 
void hr_timer_test_exit (void)
{
	struct hrtimer *hrtimer = &g_hrtimer0;
	hrtimer_cancel(hrtimer);
    pr_err("#### hr_timer_test module exit...\n");
}
