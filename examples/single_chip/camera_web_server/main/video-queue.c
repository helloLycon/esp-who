#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "app_camera.h"
#include "sdkconfig.h"
#include "tcp_bsp.h"
#include "esp_system.h"
#include "common.h"
#include "camera_error.h"
#include "sd_card_example_main.h"
#include "app_camera.h"

const char *tag = "video-queue";


video_queue  *vq_head = NULL;
video_queue  *vq_tail = NULL;

int vid_file_offset = 0;

video_queue *new_video(void) {
    video_queue *nv = (video_queue *)malloc(sizeof(video_queue));
    if(NULL == nv) {
        ESP_LOGE(tag, "malloc in %s", __func__);
        return NULL;
    }
    memset(nv, 0, sizeof(video_queue));

    nv->time = time(NULL);

    lock_vq();
    if(NULL == vq_head) {
        vq_head = vq_tail = nv;
    } else {
        vq_tail->next = nv;
        nv->prev = vq_tail;
        vq_tail = nv;
    }

    /* 重置vid offset */
    vid_file_offset = 0;
    unlock_vq();
    return nv;
}

void delete_time_video_in_sdcard(time_t t) {
    char fname[64];
    mk_sd_time_fname(t, fname);
    // Check if destination file exists before renaming
    struct stat st;
    if (stat(fname, &st) == 0) {
        // Delete it if it exists
        ESP_LOGI(tag, "unlink %s", fname);
        unlink(fname);
    }
}

/*
void drop_tail_video(void) {
    printf("=-> %s\n", __func__);
    video_queue *v = vq_tail;
    for(pic_queue *pic = v->head_pic; pic; ) {
        pic_queue *pic_next = pic->next;
        free(pic);
        pic = pic_next;
    }
    v->prev->next = NULL;
    vq_tail = v->prev;

    free(v);
}*/

void mv_video2sdcard(video_queue *v) {
    /* 已经在sdcard */
    if(v->is_in_sdcard)  {
        return;
    }
    /* 视频只存在sdcard里 */
#if 1
    pic_queue tmp_pic;
    pic_queue **ptrptr_pic = &v->head_pic;
    //int offset = 0;
    for(; *ptrptr_pic ;) {
         memcpy(&tmp_pic, *ptrptr_pic, sizeof(pic_queue));
         *ptrptr_pic = realloc(*ptrptr_pic, sizeof(pic_queue));
         if(NULL == *ptrptr_pic) {
            ESP_LOGE(tag, "realloc failed in %s, delete task", __func__);
            vTaskDelete(NULL);
         }
         memcpy(*ptrptr_pic, &tmp_pic, sizeof(pic_queue));
         //(*ptrptr_pic)->cur_time = v->time;
         //(*ptrptr_pic)->offset = offset;
         //offset += (PIC_DATA_OFFSET + tmp_pic->pic_len);
    }
#else
    for(pic_queue *pic = v->head_pic; pic; ) {
        pic_queue *pic_next = pic->next;
        free(pic);
        pic = pic_next;
    }
    v->head_pic = v->tail_pic = NULL;
#endif
    v->is_in_sdcard = true;
    xSemaphoreGive(save_pic_completed);
}

void drop_video(video_queue *v)  {
    if(v == save_pic_pointer->video) {
        /* 尝试下个视频 */
        video_queue *video = save_pic_pointer->video;
        if(video->next && video->next->head_pic) {
            /* new video */
            save_pic_pointer = video->next->head_pic;
            wr_sdcard_fp_open(true, video->next->time);
            xSemaphoreGive(vq_save_trigger);
        } else {
            /* nothing else */
            save_pic_pointer = NULL;
            wr_sdcard_fp_close();
        }
    }
    if(v == upload_pic_pointer->video) {
        /* 尝试下个视频 */
        video_queue *video = upload_pic_pointer->video;
        if(video->next && video->next->head_pic) {
            /* new video */
            upload_pic_pointer = video->next->head_pic;
            xSemaphoreGive(vq_upload_trigger);
        } else {
            /* nothing else */
            upload_pic_pointer = NULL;
            rd_sdcard_fp_close();
        }
    }

    /* handle vq */
    for(pic_queue *pic = v->head_pic; pic; ) {
        pic_queue *pic_next = pic->next;
        free(pic);
        pic = pic_next;
    }
    if(v==vq_head && v==vq_tail) {
        vq_head = vq_tail = NULL;
    } else if(v == vq_head) {
        vq_head->next->prev = NULL;
        vq_head = vq_head->next;
    } else if(v == vq_tail) {
        vq_tail->prev->next = NULL;
        vq_tail = vq_tail->prev;
    } else {
        v->next->prev = v->prev;
        v->prev->next = v->next;
    }
    /* 删除sd卡里的备份 */
    delete_time_video_in_sdcard(v->time);
    free(v);
}


/* 图片入队函数 */
void pic_in_queue(video_queue *video, int len, unsigned char *buf)
{
    if(NULL == video) {
        ESP_LOGE(tag, "video is NULL in %s", __func__);
        return;
    }

    pic_queue *cur_pic = NULL;
    if(buf) {
        cur_pic = (pic_queue *)malloc(PIC_DATA_OFFSET+len);
    } else {
        /* 尾/空节点 */
        cur_pic = (pic_queue *)malloc(sizeof(pic_queue));
    }
    if (NULL == cur_pic)
    {
        ESP_LOGE(tag, "file:%s, line:%d, malloc %d, failed!", __FILE__, __LINE__, sizeof(pic_queue));
        return ;
    }

#if  0
    cur_pic->pic_info = (unsigned char *)malloc((len + 1));
    if (NULL == cur_pic->pic_info)
    {
        free(cur_pic);
        ESP_LOGE("file:%s, line:%d, malloc %d, failed!", __FILE__, __LINE__, len);
        return ;
    }
#endif
    cur_pic->next = NULL;
    cur_pic->offset = vid_file_offset;
    vid_file_offset += (PIC_DATA_OFFSET + len);
    cur_pic->video = video;
    //cur_pic->cur_time = time(NULL);
    cur_pic->pic_len = len;
    if(buf) {
        memcpy(cur_pic->pic_info, buf, len);
    }
    
    if (NULL != video->tail_pic)
    {
        video->tail_pic->next = cur_pic;
        video->tail_pic = cur_pic;
    }
    else
    {
        video->tail_pic = cur_pic;
        video->head_pic = cur_pic;
    }
    video->num++;
}


/* 图片出队函数 */
void pic_out_queue(video_queue *video)
{
#if 0
    if(NULL == video) {
        ESP_LOGE(tag, "video is NULL in %s", __func__);
        return;
    }

    int ret;
    pic_queue *send_pic = video->head_pic;

    if (NULL == send_pic)
    {
        ESP_LOGE(tag, "file:%s, line:%d, queue is empty!", __FILE__, __LINE__);
        return ;
    }

    //printf("file:%s, line:%d, send_pic = %p, next = %p, len = %d\r\n", 
    //    __FILE__, __LINE__, send_pic, send_pic->next, send_pic->pic_len);
    ret = send_jpeg(send_pic);
    if (CAMERA_OK != ret)
    {
        ESP_LOGE(tag, "file:%s, line:%d, send_jpeg failed! ret = %d", __FILE__, __LINE__, ret);
        return ;
    }

    video->head_pic = video->head_pic->next;
    if (NULL == video->head_pic)
    {
        video->tail_pic = NULL;
    }

    //free(send_pic->pic_info);
    free(send_pic);
    send_pic = NULL;

    video->num--;
#endif
}

