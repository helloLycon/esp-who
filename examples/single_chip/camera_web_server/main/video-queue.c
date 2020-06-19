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

void dump_video(const video_queue *v) {
    printf("~~~~~~~~~~~~~~~Video-%p = ", v);
    for(const pic_queue *pic = v->head_pic; pic; pic=pic->next) {
        printf("%02x ", (uint8_t)pic);
    }
    printf("\n");
}

void dump_vq(void) {
    for(const video_queue *v = vq_head; v; v=v->next) {
        dump_video(v);
    }
}

video_queue *new_video(void) {
    printf("+++ (%s)\n", __func__);
    log_printf("新的拍摄");
    video_queue *nv = (video_queue *)malloc(sizeof(video_queue));
    if(NULL == nv) {
        ESP_LOGE(tag, "malloc in %s", __func__);
        return NULL;
    }
    memset(nv, 0, sizeof(video_queue));

    nv->time = time(NULL);

    lock_vq();
    /* 新的视频入队 */
    if(NULL == vq_head) {
        vq_head = vq_tail = nv;
    } else {
        vq_tail->next = nv;
        nv->prev = vq_tail;
        vq_tail = nv;
    }

    unlock_vq();
    return nv;
}

void delete_time_video_in_sdcard(time_t t) {
    printf("+++ (%s)\n", __func__);
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


/**
 * 重新分配整个视频图片内存，视频指向sdcard 
 */
void mv_video2sdcard(video_queue *v) {
    printf("+++ (%s)\n", __func__);
    //dump_vq();
    /* 已经在sdcard */
    if(v->is_in_sdcard)  {
        return;
    }
#if   1
    pic_queue tmp_pic;
    pic_queue **ptr_prev_pic_s_next = NULL;
    for(pic_queue *pic = v->head_pic; pic; pic = pic->next) {
        bool is_head = false, is_tail = false;
        bool reassign_upload = false;
        bool reassign_save = false;

        memcpy(&tmp_pic, pic, sizeof(pic_queue));
        /*-------------------------start---------------------------*/
        /* 是否需要更新head和tail指针 */
        if(v->head_pic == pic) {
           is_head = true;
        }
        if(v->tail_pic == pic) {
           is_tail = true;
        }
        /* 因为realloc了pic_pointer，两个任务的指针要交接 */
        if(pic == upload_pic_pointer) {
           reassign_upload = true;
        }
        if(pic == save_pic_pointer) {
           reassign_save = true;
        }
        /*-------------------------end---------------------------*/

        pic = realloc(pic, sizeof(pic_queue));
        if(NULL == pic) {
           ESP_LOGE(tag, "realloc failed in %s, delete task", __func__);
           vTaskDelete(NULL);
        }
        memcpy(pic, &tmp_pic, sizeof(pic_queue));
        if(ptr_prev_pic_s_next) {
           *ptr_prev_pic_s_next = pic;
        }
        ptr_prev_pic_s_next = &pic->next;

        /*-------------------------start---------------------------*/
        /* 重新赋值头尾指针 */
        if(is_head) {
           v->head_pic = pic;
        }
        if(is_tail) {
           v->tail_pic = pic;
        }
        /* 因为realloc了pic_pointer，两个任务的指针要交接 */
        if(reassign_upload) {
           upload_pic_pointer = pic;
        }
        if(reassign_save) {
           save_pic_pointer = pic;
        }
        /*-------------------------end---------------------------*/
    }
#else
    /* 视频只存在sdcard里 */
    pic_queue tmp_pic;
    pic_queue **ptrptr_pic = &v->head_pic;
    pic_queue **ptr_prev_pic_s_next = NULL;
    for(; *ptrptr_pic ;) {
         bool is_head = false, is_tail = false;
         bool reassign_upload = false;
         bool reassign_save = false;

         memcpy(&tmp_pic, *ptrptr_pic, sizeof(pic_queue));
         /*-------------------------start---------------------------*/
         /* 是否需要更新head和tail指针 */
         if(v->head_pic == *ptrptr_pic) {
            is_head = true;
         }
         if(v->tail_pic == *ptrptr_pic) {
            is_tail = true;
         }
         /* 因为realloc了pic_pointer，两个任务的指针要交接 */
         if(*ptrptr_pic == upload_pic_pointer) {
            reassign_upload = true;
         }
         if(*ptrptr_pic == save_pic_pointer) {
            reassign_save = true;
         }
         /*-------------------------end---------------------------*/

         *ptrptr_pic = realloc(*ptrptr_pic, sizeof(pic_queue));
         if(NULL == *ptrptr_pic) {
            ESP_LOGE(tag, "realloc failed in %s, delete task", __func__);
            vTaskDelete(NULL);
         }
         memcpy(*ptrptr_pic, &tmp_pic, sizeof(pic_queue));
         /* 重新赋值每个图片的next指针 */
         if(ptr_prev_pic_s_next) {
            *ptr_prev_pic_s_next = *ptrptr_pic;
         }
         ptr_prev_pic_s_next = &(*ptrptr_pic)->next;

         /*-------------------------start---------------------------*/
         /* 重新赋值头尾指针 */
         if(is_head) {
            v->head_pic = *ptrptr_pic;
         }
         if(is_tail) {
            v->tail_pic = *ptrptr_pic;
         }
         /* 因为realloc了pic_pointer，两个任务的指针要交接 */
         if(reassign_upload) {
            upload_pic_pointer = *ptrptr_pic;
         }
         if(reassign_save) {
            save_pic_pointer = *ptrptr_pic;
         }
         /*-------------------------end---------------------------*/

         //(*ptrptr_pic)->cur_time = v->time;
         //(*ptrptr_pic)->offset = offset;
         //offset += (PIC_DATA_OFFSET + tmp_pic->pic_len);
         *ptrptr_pic = (*ptrptr_pic)->next;
    }
#endif
    //dump_vq();
    log_printf("视频保存结束");
    printf("+++ 一个视频保存结束\n");
    v->is_in_sdcard = true;
    xSemaphoreGive(save_pic_completed);
}

void drop_video(video_queue *v)  {
    if(NULL == v) {
        return;
    }
    printf("+++ (%s)\n", __func__);

    if(NULL == upload_pic_pointer) {
        /* do nothing */
    } else if(v == upload_pic_pointer->video) {
        portENTER_CRITICAL(&time_var_spinlock);
        send_video_start_time = 0;
        portEXIT_CRITICAL(&time_var_spinlock);
        /* 尝试下个视频，函数返回后需要避免再次next */
        video_queue *video = upload_pic_pointer->video;
        if(video && video->next && video->next->head_pic) {
            /* new video */
            upload_pic_pointer = video->next->head_pic;
            xSemaphoreGive(vq_upload_trigger);
        } else {
            /* nothing else */
            upload_pic_pointer = NULL;
            rd_sdcard_fp_close();
        }
    }
    if(NULL == save_pic_pointer) {
        if(v==vq_tail) {
            /**
             * 关闭fp, 因为saveptr==NULL一定是因为save任务
             * 超前拍摄任务，正在保存tail视频，writesdfp指向tail视频
             */
            wr_sdcard_fp_close();
        }
    } else if(v == save_pic_pointer->video) {
        /* 尝试下个视频，函数返回后需要避免再次next */
        video_queue *video = save_pic_pointer->video;
        if(video && video->next && video->next->head_pic) {
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
    static uint16_t sn = 0;
    static int offset_in_vid_file = PIC_DATA_OFFSET;
    if(NULL == video) {
        ESP_LOGE(tag, "video is NULL in %s", __func__);
        return;
    }

    pic_queue *cur_pic = NULL;
    int malloc_len;
    if(buf) {
        malloc_len = PIC_DATA_OFFSET+len;
    } else {
        /* 尾/空节点 */
        malloc_len = sizeof(pic_queue);
    }
    cur_pic = (pic_queue *)malloc(malloc_len);
    if (NULL == cur_pic)
    {
        ESP_LOGE(tag, "file:%s, line:%d, malloc %d bytes, failed!", __FILE__, __LINE__, malloc_len);
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
    cur_pic->video = video;
    cur_pic->pic_len = len;
    if(buf) {
        memcpy(cur_pic->pic_info, buf, len);
    }
    
    if (NULL != video->tail_pic)
    {
        cur_pic->sn = sn++;
        cur_pic->offset_in_vid_file = offset_in_vid_file;
        offset_in_vid_file += (PIC_DATA_OFFSET + len);
        video->tail_pic->next = cur_pic;
        video->tail_pic = cur_pic;
    }
    else
    {
        /*----- 头节点 -----*/
        /* 重置图片序列号 */
        sn = 1;
        cur_pic->sn = sn++;
        /* 重置偏移量(in video file) */
        offset_in_vid_file = PIC_DATA_OFFSET;
        cur_pic->offset_in_vid_file = offset_in_vid_file;
        offset_in_vid_file += (PIC_DATA_OFFSET + len);
        video->tail_pic = cur_pic;
        video->head_pic = cur_pic;
    }
    printf(">>> new picture (%d)\n", cur_pic->sn);
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

