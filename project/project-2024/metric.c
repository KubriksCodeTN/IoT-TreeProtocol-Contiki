#include "metric.h"
/*---------------------------------------------------------------------------*/
// get the hop cost of the last message
#ifdef USE_LQI_METRIC

metric_t get_hop_cost() {
    metric_t lqi = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);
    return 1. / (lqi * lqi * lqi);
}

#else

metric_t get_hop_cost() {
    return 1;
}

#endif
/*---------------------------------------------------------------------------*/
