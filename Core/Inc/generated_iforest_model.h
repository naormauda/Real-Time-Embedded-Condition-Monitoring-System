#ifndef GENERATED_IFOREST_MODEL_H
#define GENERATED_IFOREST_MODEL_H

#include <stdbool.h>

#define IF_MODEL_NUM_FEATURES 12

bool iforest_generated_is_available(void);
float iforest_generated_default_threshold(void);
const char *iforest_generated_name(void);
float iforest_generated_predict(const float features[IF_MODEL_NUM_FEATURES]);

#endif /* GENERATED_IFOREST_MODEL_H */
