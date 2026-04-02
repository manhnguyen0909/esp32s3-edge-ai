#ifndef AI_PERSON_H
#define AI_PERSON_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ai_init(void);
int ai_run(uint8_t *image_data); // 1 = person, 0 = no person

#ifdef __cplusplus
}
#endif

#endif