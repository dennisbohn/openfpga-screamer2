/*
 * The saves (save.h): CHOICE.DAT (s2save.h) and ATTACK.DAT read at boot, their sizes into the slot table.
 */
#include "save.h"
#include "hw.h"
#include "champ.h"
#include "s2save.h"
#include "slot.h"
#include "dlog.h"
#include <string.h>

/* a slot of exactly size bytes into the SDRAM at addr: 1 if the APF read it */
static int load(uint32_t id, uint32_t addr, uint32_t size)
{
    return slot_size(id) == size && slot_read(id, 0, addr, size) == 0;
}

void save_load(void)
{
    choice_defaults();              /* Screamer 1's block, plain memory now (champ.h); its unreached code and menu_options_load() read it */
    s2save_load();
    int a = load(SAVE_ATTACK_SLOT, SAVE_ATTACK_ADDR, SAVE_ATTACK_SIZE);
    if(!a) memset((void *)(uintptr_t)SAVE_ATTACK_ADDR, 0, SAVE_ATTACK_SIZE);
    slot_set_size(SAVE_ATTACK_SLOT, SAVE_ATTACK_SIZE);
    dlog_str(a ? "save: ATTACK.DAT loaded\n" : "save: ATTACK.DAT empty\n");
}
