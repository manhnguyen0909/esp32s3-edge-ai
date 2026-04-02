#include "image_utils.h"
void resize_96x96(uint8_t *src, int w, int h, uint8_t *dst)
{
    for (int y = 0; y < 96; y++)
    {
        for (int x = 0; x < 96; x++)
        {
            int src_x = x * w / 96;
            int src_y = y * h / 96;
            dst[y * 96 + x] = src[src_y * w + src_x];
        }
    }
}