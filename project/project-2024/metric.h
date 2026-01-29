#ifndef __METRIC_H__
#define __METRIC_H__
/*---------------------------------------------------------------------------*/
#include <stdint.h> /* For numeric limits */
#include <float.h> /* For numeric limits */
#include "net/rime/rime.h"
/*---------------------------------------------------------------------------*/
#define USE_LQI_METRIC
/*---------------------------------------------------------------------------*/
// defines the deafult initial value of the parent metric (i.e the value 
// before the first beacon)
#ifdef USE_LQI_METRIC

#define DEFAULT_METRIC_VALUE FLT_MAX
typedef float metric_t; 

#else

#define DEFAULT_METRIC_VALUE UINT16_MAX
typedef uint16_t metric_t;

#endif
/*---------------------------------------------------------------------------*/
/* Gets hop cost using the selected metric */
metric_t get_hop_cost();
/*---------------------------------------------------------------------------*/
#endif /* __METRIC_H__ */
