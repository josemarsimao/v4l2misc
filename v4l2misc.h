/*
 *  V4L2 video capture My tests
 *
 *	Nome: Josemar Simão
 *
 *
 *
 */

#ifndef V4L2MISC_H_INCLUDED
#define V4L2MISC_H_INCLUDED





#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>              /// low-level i/o
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
///#include <sys/types.h>
///#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>

#include <linux/videodev2.h>





#define MAX_V4L2_DEV_NUM 127
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define SCRCLR() printf("\033[H\033[J")
#define GOTOXY(x,y) printf("\033[%d;%dH", (int)(x), (int)(y))






enum io_method {
        IO_METHOD_READ,
        IO_METHOD_USERPTR,
        IO_METHOD_MMAP,
        IO_METHOD_DMABUF,
};

enum v4l2_device_type {
        V4L2_DEV_VIDEO,
        V4L2_DEV_VBI,
        V4L2_DEV_RADIO,
        V4L2_DEV_V4L_SUBDEV,
        V4L2_DEV_SWRADIO,
        V4L2_DEV_V4L_TOUCH,
        V4L2_DEV_ALL,
};

/// STATUS
#define        DEV_NONE         0
#define        DEV_BUSY         1
#define        DEV_OPEN         2
#define        DEV_CONFIGURED   4
#define        DEV_OUR          8
#define        DEV_CAPTURING    16
#define        DEV_PROCESSING   32
#define        DEV_ALL          64


typedef struct _v4l2_id{
    enum v4l2_device_type typ;       /// identifier the device node type
    unsigned char num;               /// identifier the device node number
} v4l2_id;

typedef struct _vbuff {

        void   *start;
        size_t  length;
        struct v4l2_buffer binf;           /// included to save byteused information

} vbuff;




extern enum io_method   io;



extern int  w;
extern int  h;
extern int  force_format;
extern const char *prefixes[];
extern struct winsize          wdw;
extern int fr;
extern struct timeval t0;
extern struct timeval t1;
extern struct timeval t2;
extern int maxTH;
extern int maxTW;
extern char dev_name[];
extern unsigned char* v;


#ifdef __cplusplus
extern "C" {  // only need to export C interface if
              // used by C++ source code
#endif


/// OPEN INTERFACE

int make_list();    /// create a internal vector with device list. The list has just only the numerical part of device name.
                    /// Ex.: If device nema is /dev/video3, the list has just only number 3. The function returns the number
                    /// of devices found.
int get_video_device_list(unsigned char *dv, int dv_sz);  /// same that make_list() funciont, but using a external vector.
void show_list();
int make_video_list();
char* get_dev_name();

void init_terminal();

void set_cur_pos(int x, int y);

v4l2_id* get_v4l2_id(int idx);
void make_v4l2_path(char* dv_ph, enum v4l2_device_type typ, unsigned char num );
void get_v4l2_path(char* dv_ph, int idx);

int open_v4l2_device(char *dv_n);
int found_in_list(v4l2_id vid, char* ck);
void close_v4l2_device(int fid_l);

void choose_v4l2_device();

void show_video_format(int fid_l);
int setting_video_format(int fid_l);
void init_device(int fid_l);
void start_capturing(int fid_l);
void mainloop(int fid_l);
void stop_capturing(int fid_l);
void uninit_device(int fid_l);

int list_v4l2_capabilities(int fid_l);
int list_video_io(int fid_l);
int enumerate_controls(int fid_l);
int choose_or_enumerate_image_formats(int fid_l, struct v4l2_frmivalenum* fl, int expanded);
int choose_or_enumerate_frame_size(int fid_l, struct v4l2_frmivalenum* fl, int expanded);
int choose_or_enumerate_frame_intervals(int fid_l, struct v4l2_frmivalenum* flg, int expanded);
int set_image_format(int fid_l, __u32 format);
int set_image_size(int fid_l, int iw, int ih);
int set_image_intervals(int fid_l, __u32 n, __u32 d);

void update_terminal_size();


void show_video_list();


int is_gui_present();
int from_command_line();

void asciiart(const void *data, int isize, int ih, int iw);
void yuv_to_rgb(const void *p_rgb, const void *p_yuv, int isize, int ih, int iw);

void deallocate_image();
void allocate_image(int h, int w);
void fill_lookuptables();

void process_image(const void *p, int isize);


int xioctl(int fh, int request, void *arg);
void errno_exit(const char *s);
enum io_method check_io_method_busy(int fid_l);


#ifdef __cplusplus
}
#endif






#endif // V4L2MISC_H_INCLUDED

