#include "modules/processing/processing_port.h"

const processing_port_api_t processing_port_api = {
    .calculate_gamma = calculate_gamma,
    .fetch_amplitude = fetch_amplitude,
    .fetch_amplitude_ref = fetch_amplitude_ref};
