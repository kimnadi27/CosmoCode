#pragma once
/**
 * реестр холдеров. [Включать ПОСЛЕДНИМ]
 *
 * Порядок диспетчеризации:
 *   1. Holder_Test      -- TEST (0xF3),  любая фаза
 *   2. Holder_RR        -- RR (8-байт cmd)
 *   3. Holder_Init      -- SNRM/SARM/DISC
 *   4. Holder_UI_Normal -- UI_NORMAL (0x13)
 *   5. Holder_UI_OneWay -- UI_ONE_WAY (0x03)
 *   6. Holder_Error     -- всё остальное -> FRMR
 *
 * DM guard (до холдеров, в hdlc.h):
 *   !initialized && cmd != SNRM/SARM/TEST -> DM, return.
 */
#include "../hdlc.h"
#include "Holder_Test.h"
#include "Holder_RR.h"
#include "Holder_Init.h"
#include "Holder_UI.h"
#include "Holder_Error.h"

static inline bool Holders_Dispatch(HDLC_Handle_t *h, const HDLC_Frame_t *f)
{
    if (Holder_Test(h, f))      return true;
    if (Holder_RR(h, f))        return true;
    if (Holder_Init(h, f))      return true;
    if (Holder_UI_Normal(h, f)) return true;
    if (Holder_UI_OneWay(h, f)) return true;
    Holder_Error(h, f);
    return true;
}
