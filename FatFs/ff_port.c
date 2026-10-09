#include "FreeRTOS.h"
#include "semphr.h"
#include "ff.h"


#if FF_FS_REENTRANT == 1
void ff_mutex_create(FATFS *fs);
void ff_mutex_delete(FATFS *fs);
int ff_mutex_take(FATFS *fs, UINT timeout);
void ff_mutex_give(FATFS *fs);


void ff_mutex_create(FATFS *fs)
{
    fs->obj = xSemaphoreCreateMutex();
}

void ff_mutex_delete(FATFS *fs)
{
    if(fs->obj != NULL)
    {
        vSemaphoreDelete(fs->obj);
        fs->obj = NULL;
    }
}

int ff_mutex_take(FATFS *fs, UINT timeout)
{
    BaseType_t ret = xSemaphoreTake(fs->obj, pdMS_TO_TICKS(timeout));
    return (int)ret;
}

void ff_mutex_give(FATFS *fs)
{
    xSemaphoreGive(fs->obj);
}
#endif