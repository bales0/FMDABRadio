#pragma once
// Programme core only: opt-in does not instantiate it or enable acquisition.
#ifndef EPG_ENABLE
#define EPG_ENABLE 0
#endif

// L1 signalling queries and passive diagnostics. Independent of the programme
// core; set to 0 for a radio/SLS baseline without discovery commands.
#ifndef EPG_DISCOVERY_ENABLE
#define EPG_DISCOVERY_ENABLE 1
#endif
#if EPG_DISCOVERY_ENABLE != 0 && EPG_DISCOVERY_ENABLE != 1
#error EPG_DISCOVERY_ENABLE must be 0 or 1
#endif
#if EPG_ENABLE != 0 && EPG_ENABLE != 1
#error EPG_ENABLE must be 0 or 1
#endif
