#ifndef __VIDEO_Q_H
#define __VIDEO_Q_H


/* 图片存储队列 */
typedef struct pic_queue
{
    void *video;               /* 图片所在视频 */
    int offset;                /* offset in file */
    uint16_t pic_len;
    uint16_t sn;
    struct pic_queue *next;
    //time_t cur_time;
    unsigned char pic_info[0];
}pic_queue;

#if  0
/*--------------------------------------------
------- pic_queue <-> pic_queue_little -------
--------------------------------------------*/

/* 排列要和pic_queue相同 */
typedef struct pic_queue2 {
    void *video;               /* 图片所在视频 */
    int offset;                /* offset in file */
    uint16_t pic_len;
    struct pic_queue *next;
} pic_queue_little;
#endif

#define PIC_DATA_OFFSET    ((uint32_t)(&((pic_queue *)NULL)->pic_info))

typedef struct video_queue {
    struct video_queue *prev;
    struct video_queue *next;
    pic_queue *head_pic;
    pic_queue *tail_pic;
    time_t time;
    int num;               /* 图片数量 */
    bool complete;         /* 拍摄完成 */
    //bool save_into_sdcard_over;
    bool is_in_sdcard;     /* 告诉send线程从sd取图片 */
} video_queue;



video_queue *new_video(void);
void pic_in_queue(video_queue *video, int len, unsigned char *buf);
void pic_out_queue(video_queue *video);
void mv_video2sdcard(video_queue *v) ;
void drop_video(video_queue *v)  ;

extern video_queue  *vq_head ;
extern video_queue  *vq_tail ;




#endif
