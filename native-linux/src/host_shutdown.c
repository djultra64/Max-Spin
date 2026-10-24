/* Serialize reader shutdown before exit/handoff so a live reader cannot keep
 * refreshing its last rumble command. This does not change game preferences.
 * Own integration: djultra64. */
#include "host_shutdown.h"
#include "host.h"
#include "input/input_host.h"
#include <stdatomic.h>
static atomic_int stopped;
void topspin_host_input_shutdown(void)
{
    int expected=0;
    if(atomic_compare_exchange_strong_explicit(&stopped,&expected,1,
                memory_order_acq_rel,memory_order_acquire)) {
        xbox_HostInputStop();
        atomic_store_explicit(&stopped,2,memory_order_release);
        return;
    }
    /* Runtime Sleep releases guest execution ownership while waiting. A plain
     * blocking mutex here could deadlock the firmware guest's first stop. */
    while(atomic_load_explicit(&stopped,memory_order_acquire)!=2) Sleep(1);
}
