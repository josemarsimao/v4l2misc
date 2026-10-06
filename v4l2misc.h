/*
 *  V4L2 video capture: My tests
 *
 *	Nome: Josemar Simão
 *  Email: josemars@ifes.edu.br
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
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <linux/videodev2.h>

/* Macros de utilidade global */
#define MAX_V4L2_DEV_NUM 127
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define SCRCLR() printf("\033[H\033[J")
#define GOTOXY(x,y) printf("\033[%d;%dH", (int)(x), (int)(y))

/* Enumerações e Estruturas de Dados */
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

typedef struct _v4l2_id {
    enum v4l2_device_type typ;       /// Identificador do tipo de node do dispositivo
    unsigned char num;               /// Identificador do número do node do dispositivo
} v4l2_id;

typedef struct _vbuff {
        void   *start;
        size_t  length;
        struct v4l2_buffer binf;     /// Incluído para salvar informações de bytesused
} vbuff;



#define MAX_CAMERA_CONTROLS 128
typedef struct _camera_control_info {
    __u32 id;
    __s64 minimum;
    __s64 maximum;
    __u64 step;
    int type;
    int current_value;
    char name[32];
} camera_control_info;


/* Tabela global e contador de controles válidos encontrados */
extern camera_control_info camera_ctrl_table[MAX_CAMERA_CONTROLS];
extern int num_controls_found;




/* Variáveis Globais de Estado Compartilhadas */
extern enum io_method   io;
extern int  w;
extern int  h;
extern int  force_format;
extern int maxTH;
extern int maxTW;
extern const char *prefixes[];
extern struct winsize          wdw;
extern struct timeval t0;
extern struct timeval t1;
extern struct timeval t2;
extern __u32 pxfmt;





#ifdef __cplusplus
extern "C" {
#endif


/******************************************************************************
 * INTERFACE PÚBLICA: SEÇÃO 1 - GERENCIAMENTO DO DISPOSITIVO E CAPTURA
 *****************************************************************************/
void errno_exit(const char *s);
int get_errno_description(void);
void explain_v4l2_error(const char *context_message);
enum io_method check_io_method_busy(int fid_l);
int xioctl(int fh, int request, void *arg);
int open_v4l2_device(char *dv_n);
void close_v4l2_device(int fid_l);
int setting_video_format(int fid_l);
int set_image_format(int fid_l, __u32 format);
int set_image_size(int fid_l, int iw, int ih);
int set_image_intervals(int fid_l, __u32 n, __u32 d);
void init_device(int fid_l);
void uninit_device(int fid_l);
void start_capturing(int fid_l);
void stop_capturing(int fid_l);
void mainloop(int fid_l);

/******************************************************************************
 * INTERFACE PÚBLICA: SEÇÃO 2 - INTERFACE, TERMINAL E EXIBIÇÃO TEXTUAL
 *****************************************************************************/
void init_terminal();
void set_cur_pos(int x, int y);
void update_terminal_size();
void show_video_format(int fid_l);
int list_v4l2_capabilities(int fid_l);
int list_video_io(int fid_l);
int populate_controls_table(int fid_l);
int enumerate_controls(int fid_l);
void enumerate_menu(struct v4l2_query_ext_ctrl *qXctrl, int fid_l);
int enumerate_video_standards(int fid_l);
int enumerate_audio_input(int fid_l);
int enumerate_audio_output(int fid_l);
int enumerate_video_input(int fid_l);
int enumerate_video_output(int fid_l);
int choose_or_enumerate_image_formats(int fid_l, struct v4l2_frmivalenum* fl, int expanded);
int choose_or_enumerate_frame_size(int fid_l, struct v4l2_frmivalenum* fl, int expanded);
int choose_or_enumerate_frame_intervals(int fid_l, struct v4l2_frmivalenum* flg, int expanded);
void asciiart(const void *data, int isize, int ih, int iw);
unsigned char* convert_to_grayscale(const unsigned char* raw_buffer, unsigned long buffer_size);
/* Descomprime buffers compactados MJPEG/JPEG para Grayscale 8-bit usando a libjpeg */
unsigned char* mjpeg_to_grayscale(const unsigned char* jpeg_buffer, unsigned long jpeg_size, int *out_h, int *out_w);
/* Extrai o canal de brilho (Y) de formatos intercalados (YUYV / UYVY) saltando os bytes de croma */
unsigned char* extract_yuyv_brightness(const unsigned char* raw_buffer, unsigned long buffer_size);
/* Copia em alta velocidade o plano Y contíguo que já vem isolado no início de formatos planares (NV12 / YUV420) */
unsigned char* extract_planar_brightness(const unsigned char* raw_buffer, unsigned long buffer_size);
/* Converte formatos de cor direta (RGB24 / BGR24) para Grayscale 8-bit usando aritmética rápida de inteiros e shift de bits */
unsigned char* rgb_to_grayscale(const unsigned char* raw_buffer, unsigned long buffer_size);


/******************************************************************************
 * INTERFACE PÚBLICA: SEÇÃO 3 - VARREDURA DE SISTEMA, ROTAS E HISTÓRICO
 *****************************************************************************/
char* get_dev_name();
v4l2_id* get_v4l2_id(int idx);
void choose_v4l2_device();
int make_v4l2_list();
int make_video_list();
void show_v4l2_list();
void show_video_list();
void make_v4l2_path(char* dv_ph, enum v4l2_device_type typ, unsigned char num);
void get_v4l2_path(char* dv_ph, int idx);
int found_in_list(v4l2_id vid, char* ck);
int is_gui_present();
int from_command_line();

/* Funções mantidas temporariamente para fins didáticos / legado gráfico */
void fill_lookuptables();
void yuv_to_rgb(const void *p_rgb, const void *p_yuv, int isize, int ih, int iw);
void allocate_image(int h, int w);
void deallocate_image();

#ifdef __cplusplus
}
#endif

#endif // V4L2MISC_H_INCLUDED

