#pragma once
// Live runtime is enabled on the target; isolated legacy host suites opt in.
#ifndef EPG_ENABLE
#ifdef FMDAB_HOST_TEST
#define EPG_ENABLE 0
#else
#define EPG_ENABLE 1
#endif
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
