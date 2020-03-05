#ifndef __CAMERA_ERROR_H__
#define __CAMERA_ERROR_H__

typedef enum camera_error_no
{
    CAMERA_OK = 0,

    CAMERA_ERROR_CREATE_SOCKET_FAILED = 1,
    CAMERA_ERROR_MALLOC_FAILED = 2,
    CAMERA_ERROR_SEND_FAILED = 3,
}camera_error_no;

#endif

