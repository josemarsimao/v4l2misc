/*
 *  V4L2 video capture example
 *
 *  Nome: Josemar Simão
 *  email: josemars@ifes.edu.br
 *
 *  This program can be used and distributed without restrictions.
 *
 *  This program is provided with the V4L2 API
 *  see http://linuxtv.org/docs.php for more information
 *
 */

#include "v4l2misc.h"
#include "kbhit.h"
#include <ctype.h>
/* Biblioteca externa de baixo nível para tratamento de MJPEG */
#include <jpeglib.h>

char dev_name[256];
static v4l2_id v4l2_device_list[MAX_V4L2_DEV_NUM];
static int          num_dev_found = 0;
vbuff               *buffers;
unsigned int        num_buf;
int                 force_format;
enum io_method      io = -1;
int                 w = 0;
int                 h = 0;
struct winsize      wdw;
int                 maxTH = 0;
int                 maxTW = 0;

/// ***************************
/// FRAME RATE CALCULATION
int fr = 0;
struct timeval t0;
struct timeval t1;
struct timeval t2;
/// ***************************

/// Image Pixels Format
__u32 pxfmt;
camera_control_info camera_ctrl_table[MAX_CAMERA_CONTROLS];
int num_controls_found = 0;



static void init_read(unsigned int buffer_size);
static void init_mmap(int fid_l);
static void init_userp(unsigned int buffer_size, int fid_l);
static void init_dma(int fid_l);




///    - Position the Cursor:
///      \033[<L>;<C>H
///         Or
///      \033[<L>;<C>f
///      puts the cursor at line L and column C.
///    - Move the cursor up N lines:
///      \033[<N>A
///    - Move the cursor down N lines:
///      \033[<N>B
///    - Move the cursor forward N columns:
///      \033[<N>C
///    - Move the cursor backward N columns:
///      \033[<N>D
///    - Clear the screen, move to (0,0):
///      \033[2J
///    - Erase to end of line:
///      \033[K
///    - Save cursor position:
///      \033[s
///    - Restore cursor position:
///      \033[u
///    - Hide the cursor
///      \e[?25l
///    - Show the cursor
///      \e[?25h

/// https://linuxtv.org/downloads/v4l-dvb-apis/kapi/v4l2-dev.html#video-device-registration
///VFL_TYPE_GRABBER    /dev/videoX         for video input/output devices
///VFL_TYPE_VBI        /dev/vbiX           for vertical blank data (i.e. closed captions, teletext)
///VFL_TYPE_RADIO      /dev/radioX         for radio tuners
///VFL_TYPE_SUBDEV     /dev/v4l-subdevX    for V4L2 subdevices
///VFL_TYPE_SDR        /dev/swradioX       for Software Defined Radio (SDR) tuners
///VFL_TYPE_TOUCH      /dev/v4l-touchX     for touch sensors

const char *prefixes[] = {
	"video",
	"vbi",
	"radio",
	"v4l-subdev",
    "swradio",
    "v4l-touch",
	NULL
};



/******************************************************************************
 * SECTION 1: FUNÇÕES ESPECÍFICAS, TRATAMENTO DE DRIVER E CAPTURA (INÍCIO)
 *
 * Objetivo: Realizar chamadas IOCTL de baixo nível, alocar e mapear buffers,
 * e gerenciar diretamente o fluxo de inicialização e loop de frames do hardware.
 *****************************************************************************/

/* Sai do programa reportando erros do sistema através de uma mensagem textual */
void errno_exit(const char *s) {

    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);

}

/* Imprime a descrição textual amigável do erro atual e retorna o código numérico do errno */
int get_errno_description(void) {

    fprintf(stderr, "V4L2 System Error (errno %d): %s\n", errno, strerror(errno));

    return errno;
}

/*
 * Exibe um diagnóstico detalhado e explicativo sobre os erros REAIS do V4L2,
 * auxiliando no debug de concorrência, largura de banda e alocação de buffers.
 */
void explain_v4l2_error(const char *context_message) {
    fprintf(stderr, "\n[V4L2 DIAGNÓSTICO] Erro em: %s\n", context_message);
    fprintf(stderr, "Mensagem do Sistema: %s (Código: %d)\n", strerror(errno), errno);
    fprintf(stderr, "Causa Provável e Sugestões:\n");

    switch (errno) {
        case EPERM:
            fprintf(stderr, " -> Operação não permitida. Privilégios insuficientes para controlar o hardware.\n");
            fprintf(stderr, " -> Sugestão: Execute o programa como root/sudo ou ajuste as permissões do udev.\n");
            break;

        case ENOENT:
            fprintf(stderr, " -> Arquivo de dispositivo não encontrado no sistema.\n");
            fprintf(stderr, " -> Sugestão: Verifique se o node em /dev/videoX realmente existe ou se foi digitado corretamente.\n");
            break;

        case EACCES:
            fprintf(stderr, " -> Acesso negado. Modo de abertura inadequado para as capacidades do hardware.\n");
            fprintf(stderr, " -> Sugestão: Verifique se você abriu o dispositivo em modo de leitura/escrita correto ou se há restrição de permissão.\n");
            break;

        case EBUSY:
            fprintf(stderr, " -> O dispositivo de vídeo já está ocupado transmitindo stream ou sendo controlado por outro processo/thread.\n");
            fprintf(stderr, " -> Sugestão: Certifique-se de que softwares como OBS, Zoom ou outra instância gráfica estejam fechados.\n");
            break;

        case ENODEV:
            fprintf(stderr, " -> O dispositivo físico desapareceu ou foi removido do barramento de comunicação do sistema.\n");
            fprintf(stderr, " -> Sugestão: Verifique se o cabo USB da câmera não sofreu mau contato ou foi desconectado.\n");
            break;

        case ENOMEM:
            fprintf(stderr, " -> Falha crítica de memória! O sistema não conseguiu alocar o espaço solicitado para os buffers de vídeo.\n");
            fprintf(stderr, " -> Sugestão: Diminua a quantidade de buffers mapeados (req.count) no VIDIOC_REQBUFS ou libere memória RAM.\n");
            break;

        case ENOSPC:
            fprintf(stderr, " -> Limite de largura de banda excedido no barramento USB!\n");
            fprintf(stderr, " -> Sugestão: O controlador USB não aguenta o fluxo de dados. Reduza a resolução, reduza o frame rate ou mude para um formato comprimido (ex: MJPEG).\n");
            break;

        case EINVAL:
            fprintf(stderr, " -> Argumento ou comando ioctl inválido. O driver da câmera não reconhece ou não aceita o parâmetro enviado.\n");
            fprintf(stderr, " -> Sugestão: Verifique se o método de I/O selecionado (MMAP/USERPTR) ou o Pixel Format é realmente suportado pelas capacidades da câmera.\n");
            break;

        case EAGAIN:
            fprintf(stderr, " -> Buffer de imagem temporariamente indisponível (Operação não-bloqueante).\n");
            fprintf(stderr, " -> Sugestão: O driver ainda está capturando o quadro. Aguarde o próximo ciclo de sincronização do select().\n");
            break;

        default:
            fprintf(stderr, " -> Erro geral de sistema/POSIX não mapeado especificamente para o subsistema de vídeo.\n");
            break;
    }
    fprintf(stderr, "----------------------------------------------------------------------\n\n");
}

/* Wrapper seguro para a chamada ioctl do sistema, tratando interrupções por sinais */
int xioctl(int fh, int request, void *arg) {

    int r;

    do {

        r = ioctl(fh, request, arg);

    } while (-1 == r && EINTR == errno);

    return r;

}

/* Abre o descritor de arquivo do dispositivo caractere V4L2 em modo não-bloqueante */
int open_v4l2_device(char *dv_n) {

    int fid_l = 0;

    struct stat st;

    if(!dv_n) {
        if(dev_name[0]){
            dv_n = dev_name;
        }else{
            fprintf(stderr, "There is not a device name\n");
            exit(EXIT_FAILURE);
        }
    }

    if (-1 == stat(dv_n, &st)) {
            fprintf(stderr, "Cannot identify '%s': %d, %s\n", dv_n, errno, strerror(errno));
            exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode)) {
            fprintf(stderr, "%s is no device\n", dv_n);
            exit(EXIT_FAILURE);
    }

    fid_l = open(dv_n, O_RDWR | O_NONBLOCK, 0);
    if (-1 == fid_l) {
        explain_v4l2_error("Abertura do arquivo de dispositivo (open)");
        fprintf(stderr, "Cannot open '%s': %d, %s\n", dv_n, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }


    return fid_l;

}

/* Fecha o descritor de arquivo associado ao dispositivo de vídeo ativo */
void close_v4l2_device(int fid_l) {

    if (-1 == close(fid_l)) {
        errno_exit("close");
    }

}

/// ***********************************************************************************
/// ************ GET AND SET IMAGE FORMATS, FRAME SIZE AND FRAME INTERVALS ************
/// ***********************************************************************************

/* Configura o pixel format e a resolução interativamente via menu no terminal */
int setting_video_format(int fid_l){

    struct v4l2_frmivalenum fl;

    if (fid_l < 0){
        exit(EXIT_FAILURE);
    }

    /// *********************************    choose video formats

    printf("\n\n Available image formats for %s:\n", dev_name);

    choose_or_enumerate_image_formats(fid_l, &fl, 0);

    choose_or_enumerate_frame_size(fid_l, &fl, 0);

    choose_or_enumerate_frame_intervals(fid_l, &fl, 0);

    /// ************************* SET NEW FORMAT

    w = fl.width;
    h = fl.height;

    pxfmt = fl.pixel_format;

    set_image_format(fid_l, fl.pixel_format);
    set_image_size(fid_l, fl.width, fl.height);
    set_image_intervals(fid_l, fl.discrete.numerator, fl.discrete.denominator);

    return 0;

}

/* Modifica diretamente a estrutura interna do driver para alterar o pixel format */
int set_image_format(int fid_l, __u32 format) {

    struct v4l2_format fm;
    /// Get current format
    memset(&fm, 0, sizeof(fm));
    fm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fid_l, VIDIOC_G_FMT, &fm))
        errno_exit("VIDIOC_G_FMT");

    fm.fmt.pix.pixelformat        = format;

    /// Set new format
    if (-1 == xioctl(fid_l, VIDIOC_S_FMT, &fm)){
        explain_v4l2_error("Definição do formato de pixel e resolução (VIDIOC_S_FMT)");
        errno_exit("VIDIOC_S_FMT");
    }

    return fid_l;

}

/* Configura a largura e a altura do frame desejado no driver */
int set_image_size(int fid_l, int iw, int ih) {

    struct v4l2_format fm;
///    unsigned int min;

    /// Get current format
    memset(&fm, 0, sizeof(fm));
    fm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fid_l, VIDIOC_G_FMT, &fm))
        errno_exit("VIDIOC_G_FMT");

    fm.fmt.pix.width              = iw; ///fz.discrete.width;
    fm.fmt.pix.height             = ih; ///fz.discrete.height;


    /// Set new format
    if (-1 == xioctl(fid_l, VIDIOC_S_FMT, &fm)){
        explain_v4l2_error("Definição do formato de pixel e resolução (VIDIOC_S_FMT)");
        errno_exit("VIDIOC_S_FMT");
    }


    /// Removes "buggy driver paranoia"
    /// https://patchwork.kernel.org/patch/985782/


    return fid_l;
}

/* Ajusta os numeradores/denominadores do framerate desejado para a captura */
int set_image_intervals(int fid_l, __u32 n, __u32 d) {

    struct v4l2_streamparm sp;
    /// Get current frame interval
    memset(&sp, 0, sizeof(sp));
    sp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fid_l, VIDIOC_G_PARM, &sp))
            errno_exit("VIDIOC_G_PARM");

    if(n == 0){

        n = 1;
        d = 5;

    }

    /// Set the frame interval
    sp.parm.capture.timeperframe.numerator = n; ///fl.discrete.numerator;
    sp.parm.capture.timeperframe.denominator = d; ///fl.discrete.denominator;

    if (-1 == xioctl(fid_l, VIDIOC_S_PARM, &sp)){
        explain_v4l2_error("Definição do formato de pixel e resolução (VIDIOC_S_FMT)");
        errno_exit("VIDIOC_S_FMT");
    }

    if (-1 == xioctl(fid_l, VIDIOC_G_PARM, &sp)){
        explain_v4l2_error("Definição do formato de pixel e resolução (VIDIOC_S_FMT)");
        errno_exit("VIDIOC_G_PARM");
    }


    return fid_l;

}

/* Prepara as estruturas, valida recursos de I/O do driver e aloca os buffers corretos */
void init_device(int fid_l){

///    struct v4l2_requestbuffers req;

    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;


    if (-1 == xioctl(fid_l, VIDIOC_QUERYCAP, &cap)) {

        if (EINVAL == errno) {

            fprintf(stderr, "%s is no V4L2 device\n", dev_name);
            exit(EXIT_FAILURE);

        } else {

            errno_exit("VIDIOC_QUERYCAP");

        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){

        fprintf(stderr, "%s is no video capture device\n", dev_name);
        exit(EXIT_FAILURE);

    }


    if(io < IO_METHOD_READ || io > IO_METHOD_DMABUF){

        io = check_io_method_busy(fid_l);

    }else{

        switch (io) {

            case IO_METHOD_READ:
                if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
                        fprintf(stderr, "%s does not support read i/o\n", dev_name);
                        exit(EXIT_FAILURE);
                }
                break;

            case IO_METHOD_MMAP:
            case IO_METHOD_USERPTR:
                if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
                        fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
                        exit(EXIT_FAILURE);
                }
                break;

            case IO_METHOD_DMABUF:

                break;

        }

    }


    /* Select video input, video standard and tune here. */

    /*
    VIDIOC_CROPCAP
    VIDIOC_S_CROP
    VIDIOC_G_CROP
    VIDIOC_S_FMT
    VIDIOC_G_FMT
    */


    CLEAR(cropcap);

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fid_l, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if (-1 == xioctl(fid_l, VIDIOC_S_CROP, &crop)) {
            switch (errno) {
                case EINVAL:
                    /* Cropping not supported. */
                    break;
                default:
                    /* Errors ignored. */
                    break;
            }
            printf("error found\n");
        }
    } else {
            /* Errors ignored. */
            printf("error found\n");
    }


    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (force_format) {

        fmt.fmt.pix.width       = 640;
        fmt.fmt.pix.height      = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;

        if (-1 == xioctl(fid_l, VIDIOC_S_FMT, &fmt))
                errno_exit("VIDIOC_S_FMT");

        /* Note VIDIOC_S_FMT may change width and height. */

    } else {

        /* Preserve original settings as set by v4l2-ctl for example */
        if (-1 == xioctl(fid_l, VIDIOC_G_FMT, &fmt))
                errno_exit("VIDIOC_G_FMT");

    }

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
            fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
            fmt.fmt.pix.sizeimage = min;



    /// ***********************************************************
    /// Prepare the buffer by allocating memory space for one image
    /// ***********************************************************
    switch (io) {

        case IO_METHOD_READ:
            init_read(fmt.fmt.pix.sizeimage);
            break;

        case IO_METHOD_MMAP:
            init_mmap(fid_l);
            break;

        case IO_METHOD_USERPTR:
            init_userp(fmt.fmt.pix.sizeimage, fid_l);
            break;

        case IO_METHOD_DMABUF:
            init_dma(fid_l);
            break;

    }

}

/* Inicializa buffers para I/O clássico baseados em chamadas diretas de leitura read() */
static void init_read(unsigned int buffer_size) {

    buffers = (vbuff*) calloc(1, sizeof(*buffers));

    if (!buffers) {
            explain_v4l2_error("Requisição de alocação de ponteiro para buffers (VIDIOC_REQBUFS)");
            fprintf(stderr, "Out of memory\n");
            exit(EXIT_FAILURE);
    }

    buffers[0].length = buffer_size;
    buffers[0].start = malloc(buffer_size);

    if (!buffers[0].start) {
            explain_v4l2_error("Requisição de alocação de buffers via READ (VIDIOC_REQBUFS)");
            fprintf(stderr, "Out of memory\n");
            exit(EXIT_FAILURE);
    }

}

/* Aloca e mapeia buffers de memória compartilhada entre kernel space e user space */
static void init_mmap(int fid_l) {

    struct v4l2_requestbuffers req; /// Used to allocate memory on driver space

    CLEAR(req);



/// ****************************************
/// ************ CREATE BUFFERS ************
/// ****************************************

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fid_l, VIDIOC_REQBUFS, &req)) {
            explain_v4l2_error("Requisição de alocação de buffers via MMAP (VIDIOC_REQBUFS)");
            if (EINVAL == errno) {
                    fprintf(stderr, "%s does not support memory mapping\n", dev_name);
                    exit(EXIT_FAILURE);
            } else {
                    errno_exit("VIDIOC_REQBUFS");
            }
    }

    if (req.count < 2) {
        /* You may need to free the buffers here. */
        fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
        exit(EXIT_FAILURE);
    }

    buffers = (vbuff*)calloc(req.count, sizeof(*buffers));

    if (!buffers) {

        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);

    }



/// ***********************************************************
/// ************ MAP BUFFERS ADDRESS TO USER SPACE ************
/// ***********************************************************



    for (num_buf = 0; num_buf < req.count; ++num_buf) {

        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = num_buf;

        if (-1 == xioctl(fid_l, VIDIOC_QUERYBUF, &buf))
                errno_exit("VIDIOC_QUERYBUF");

        buffers[num_buf].length = buf.length; /* remember for munmap() */
        buffers[num_buf].start =
                mmap(NULL /* start anywhere */,
                      buf.length,
                      PROT_READ | PROT_WRITE /* required */,
                      MAP_SHARED /* recommended */,
                      fid_l, buf.m.offset);

        if (MAP_FAILED == buffers[num_buf].start)
            /* If you do not exit here you should unmap() and free()
            the buffers mapped so far. */
            errno_exit("mmap");

    }

}

/* Aloca buffers do lado do espaço de usuário para repasse direto ao driver */
static void init_userp(unsigned int buffer_size, int fid_l) {

        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count  = 4;
        req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_USERPTR;

        if (-1 == xioctl(fid_l, VIDIOC_REQBUFS, &req)) {
                explain_v4l2_error("Requisição de alocação de buffers via USERPTR (VIDIOC_REQBUFS)");
                if (EINVAL == errno) {

                        fprintf(stderr, "%s does not support user pointer i/o\n", dev_name);
                        exit(EXIT_FAILURE);

                } else {

                        errno_exit("VIDIOC_REQBUFS");

                }
        }

        buffers = (vbuff*)calloc(4, sizeof(*buffers));

        if (!buffers) {

                fprintf(stderr, "Out of memory\n");
                exit(EXIT_FAILURE);

        }

        for (num_buf = 0; num_buf < 4; ++num_buf) {

                buffers[num_buf].length = buffer_size;
                buffers[num_buf].start = malloc(buffer_size);

                if (!buffers[num_buf].start) {

                        fprintf(stderr, "Out of memory\n");
                        exit(EXIT_FAILURE);

                }
        }
}

/* Espaço reservado para futura inicialização via Direct Memory Access (DMA) */
static void init_dma(int fid_l) {


}

/* Libera todos os mapeamentos de memória (munmap) e buffers do sistema alocados */
void uninit_device(int fid_l) {

    unsigned int i;
    struct v4l2_requestbuffers req;

    switch (io) {

        case IO_METHOD_READ:
            free(buffers[0].start);
            break;

        case IO_METHOD_MMAP:
            for (i = 0; i < num_buf; ++i){

                if (-1 == munmap(buffers[i].start, buffers[i].length))
                    errno_exit("munmap");

            }

            /// ***********************************************
            /// ************ DEALLOCATE DRIVER BUFFER *********
            /// ***********************************************

            memset(&req, 0, sizeof(req));
            req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            req.memory = V4L2_MEMORY_MMAP;
            req.count = 0;
            if (-1 == xioctl (fid_l, VIDIOC_REQBUFS, &req)){
                if (errno != EBUSY){
                    printf("error in check_io_method_busy function\n");
                }
            }

            break;

        case IO_METHOD_USERPTR:
            for (i = 0; i < num_buf; ++i){

                free(buffers[i].start);

            }
            break;
        case IO_METHOD_DMABUF:

            break;

    }

    free(buffers);

}

/* Enfileira os buffers iniciais no driver e ativa a transmissão do streaming */
void start_capturing(int fid_l) {

    unsigned int i;
    enum v4l2_buf_type type;



    /// ***********************************************************
    /// Queue buffer pointer to driver ????
    /// ***********************************************************
    switch (io) {
        case IO_METHOD_READ:
            /* Nothing to do. */
            break;

        case IO_METHOD_MMAP:
            for (i = 0; i < num_buf; ++i) {
                struct v4l2_buffer buf;

                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;

                if (-1 == xioctl(fid_l, VIDIOC_QBUF, &buf))
                        errno_exit("VIDIOC_QBUF");
            }
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (-1 == xioctl(fid_l, VIDIOC_STREAMON, &type)){
                explain_v4l2_error("Ativação do fluxo de transmissão local (VIDIOC_STREAMON)");
                errno_exit("VIDIOC_STREAMON");
            }
            break;

        case IO_METHOD_USERPTR:
            for (i = 0; i < num_buf; ++i) {
                struct v4l2_buffer buf;

                CLEAR(buf);
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_USERPTR;
                buf.index = i;
                buf.m.userptr = (unsigned long)buffers[i].start;
                buf.length = buffers[i].length;

                if (-1 == xioctl(fid_l, VIDIOC_QBUF, &buf))
                        errno_exit("VIDIOC_QBUF");
            }
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (-1 == xioctl(fid_l, VIDIOC_STREAMON, &type)){
                explain_v4l2_error("Ativação do fluxo de transmissão local (VIDIOC_STREAMON)");
                errno_exit("VIDIOC_STREAMON");
            }
            break;

        case IO_METHOD_DMABUF:

            break;

    }

}

/* Desativa a transmissão do streaming requisitando interrupção do envio de dados */
void stop_capturing(int fid_l) {

    enum v4l2_buf_type type;

    switch (io) {
        case IO_METHOD_READ:
            /* Nothing to do. */
            break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (-1 == xioctl(fid_l, VIDIOC_STREAMOFF, &type)){
                explain_v4l2_error("Interrupção do fluxo de transmissão local (VIDIOC_STREAMOFF)");
                errno_exit("VIDIOC_STREAMOFF");
            }
            break;
        case IO_METHOD_DMABUF:
            break;

    }

}

/* Captura e retira um buffer pronto da fila de execução e chama o processamento */
static int read_frame(int fid_l) {

        struct v4l2_buffer buf;
        unsigned int i;

        switch (io) {

            case IO_METHOD_READ:

                if (-1 == read(fid_l, buffers[0].start, buffers[0].length)) {

                    switch (errno) {

                        case EAGAIN:
                            return 0;

                        case EIO:
                            /* Could ignore EIO, see spec. */

                            /* fall through */

                        default:
                            errno_exit("read");

                    }

                }

                process_image(buffers[0].start, buffers[0].length);
                break;

            case IO_METHOD_MMAP:

                CLEAR(buf);

                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;

                if (-1 == xioctl(fid_l, VIDIOC_DQBUF, &buf)) {

                    switch (errno) {
                        case EAGAIN:
                            return 0;

                        case EIO:
                            // Could ignore EIO, see spec.

                            // fall through

                        default:
                            errno_exit("VIDIOC_DQBUF");
                    }
                    printf("error found\n");
                }

                assert(buf.index < num_buf);
                buffers[buf.index].binf = buf;      /// included to store byteused information

                process_image(buffers[buf.index].start, buf.bytesused);

                if (-1 == xioctl(fid_l, VIDIOC_QBUF, &buf))
                        errno_exit("VIDIOC_QBUF");

                break;

            case IO_METHOD_USERPTR:
                CLEAR(buf);

                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_USERPTR;

                if (-1 == xioctl(fid_l, VIDIOC_DQBUF, &buf)) {
                    switch (errno) {
                        case EAGAIN:
                            return 0;

                        case EIO:
                            /* Could ignore EIO, see spec. */

                            /* fall through */

                        default:
                            errno_exit("VIDIOC_DQBUF");
                    }
                    printf("error found\n");
                }

                for (i = 0; i < num_buf; ++i)
                    if (buf.m.userptr == (unsigned long)buffers[i].start && buf.length == buffers[i].length)
                        break;

                assert(i < num_buf);

                buffers[i].binf = buf;              /// included to store byteused information

                process_image((void *)buf.m.userptr, buf.bytesused);

                if (-1 == xioctl(fid_l, VIDIOC_QBUF, &buf))
                        errno_exit("VIDIOC_QBUF");

                break;

            case IO_METHOD_DMABUF:

                break;
        }

        return 1;

}

/* Laço controlado por teclado que aguarda via select() e processa frames continuamente */
void mainloop(int fid_l){

  char ctrlr = '.';

  SCRCLR();


  while (ctrlr != 'q'){
    while(_kbhit()){
      ctrlr = getch();
    }

    for (;;) {
      fd_set fds;
      struct timeval tv;
      int r=1;

      FD_ZERO(&fds);
      FD_SET(fid_l, &fds);

      /* Timeout. */
      tv.tv_sec = 5;
      tv.tv_usec = 0;

      r = select(fid_l + 1, &fds, NULL, NULL, &tv);

      if(-1 == r){
        if(EINTR == errno) continue;
        errno_exit("select");
      }

      if(0 == r){
        fprintf(stderr, "select timeout\n");
        exit(EXIT_FAILURE);
      }

      if (read_frame(fid_l)){
        break;
      }else{

      }

      /* EAGAIN - continue select loop. */
    }
  }

}


/******************************************************************************
 * SECTION 2: EXIBIÇÃO EM TEXTO, SELEÇÃO E EXIBIÇÃO DE METADADOS (MEIO)
 *
 * Objetivo: Formatar saídas textuais para o usuário, tratar posicionamento,
 * renderizar o ASCII art, e exibir informações de formatos/controles suportados.
 *****************************************************************************/


/* Ajusta as variáveis de tempo e armazena as dimensões originais do terminal */
void init_terminal(){

    gettimeofday(&t0, NULL);
    gettimeofday(&t1, NULL);
    gettimeofday(&t2, NULL);


    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wdw);
    if(maxTH != wdw.ws_row || maxTW != wdw.ws_col){
      SCRCLR();
      maxTH = wdw.ws_row;
      maxTW = wdw.ws_col;
    }

}

/* Move o cursor para coordenadas X e Y específicas do terminal usando ANSI escape */
void set_cur_pos(int x, int y){

    printf("\033[%d;%dH", y, x);

}

/* Consulta o tamanho físico atual da janela do terminal de comandos */
void update_terminal_size(){
/// ******************************************
/// How to get the terminal dimensions?
/// answer -->>>   https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
/// First way:  (Do not work on Raspberry Pi 3 running  Ubuntu Server)
    //  maxH = atol(getenv("LINES"));
    //  maxW = atol(getenv("COLUMNS"));
/// Second way:
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wdw);
    if(maxTH != wdw.ws_row || maxTW != wdw.ws_col){
        maxTH = wdw.ws_row;
        maxTW = wdw.ws_col;
    }
/// ******************************************
}

/* Retorna a altura máxima (linhas) disponível no terminal ativo */
int get_terminal_max_height(){
    struct winsize wdw;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wdw);
    return wdw.ws_row;
}

/* Retorna a largura máxima (colunas) disponível no terminal ativo */
int get_terminal_max_width(){
    struct winsize wdw;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &wdw);
    return wdw.ws_col;
}

/* Imprime no terminal o status do pixel format atual, tamanho do frame e intervalos */
void show_video_format(int fid_l){

    struct v4l2_format fm;
    struct v4l2_streamparm sp;
    char p[5] = {0,0,0,0,0};


    /// Get current format
    memset(&fm, 0, sizeof(fm));
    fm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fid_l, VIDIOC_G_FMT, &fm))
        errno_exit("VIDIOC_G_FMT");

    /// Get current frame interval
    memset(&sp, 0, sizeof(sp));
    sp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fid_l, VIDIOC_G_PARM, &sp))
            errno_exit("VIDIOC_G_PARM");

    *((int*)p) = fm.fmt.pix.pixelformat;

    printf("Pixel format: %s\n", p);
    printf("Resolution: %d x %d\n", fm.fmt.pix.width, fm.fmt.pix.height);
    printf("Bytes per line: %d\n", fm.fmt.pix.bytesperline);
    printf("Size of image: %d\n", fm.fmt.pix.sizeimage);
    printf("Color space: %d\n", fm.fmt.pix.colorspace);
    printf("Intervals: %d/%d\n", sp.parm.capture.timeperframe.numerator, sp.parm.capture.timeperframe.denominator);
}

/* Varre e exibe em formato string detalhado todas as flags de capacidades do driver */
int list_v4l2_capabilities(int fid_l) {

    int r;
    struct v4l2_capability cap;

    r = xioctl(fid_l, VIDIOC_QUERYCAP, &cap);

    if(r != 0) {

        printf("%s is no V4L2 device\n", dev_name);

    } else {

        printf("Driver name: %s\n", cap.driver);
        printf("Device name: %s\n", cap.card);
        printf("Bus location: %s\n", cap.bus_info);
        printf("Version: %u.%u.%u\n",(cap.version >> 16) & 0xFF,(cap.version >> 8) & 0xFF,cap.version & 0xFF);
        if(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) printf("The device supports the single-planar API through the Video Capture interface.\n");
        if(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) printf("The device supports the multi-planar API through the Video Capture interface.\n");
        if(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) printf("The device supports the single-planar API through the Video Output interface.\n");
        if(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) printf("The device supports the multi-planar API through the Video Output interface.\n");
        if(cap.capabilities & V4L2_CAP_VIDEO_M2M) printf("The device supports the single-planar API through the Video Memory-To-Memory interface.\n");
        if(cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) printf("The device supports the multi-planar API through the Video Memory-To-Memory interface.\n");
        if(cap.capabilities & V4L2_CAP_VIDEO_OVERLAY) printf("The device supports the Video Overlay interface.\n");
        if(cap.capabilities & V4L2_CAP_VBI_CAPTURE) printf("The device supports the Raw VBI Capture interface.\n");
        if(cap.capabilities & V4L2_CAP_VBI_OUTPUT) printf("The device supports the Raw VBI Output interface.\n");
        if(cap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) printf("The device supports the Sliced VBI Capture interface.\n");
        if(cap.capabilities & V4L2_CAP_SLICED_VBI_OUTPUT) printf("The device supports the Sliced VBI Output interface.\n");
        if(cap.capabilities & V4L2_CAP_RDS_CAPTURE) printf("The device supports the RDS capture interface.\n");
        if(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY) printf("The device supports the Video Output Overlay (OSD) interface.\n");
        if(cap.capabilities & V4L2_CAP_HW_FREQ_SEEK) printf("The device supports the VIDIOC_S_HW_FREQ_SEEK ioctl for hardware frequency seeking.\n");
        if(cap.capabilities & V4L2_CAP_RDS_OUTPUT) printf("The device supports the RDS output interface.\n");
        if(cap.capabilities & V4L2_CAP_TUNER) printf("The device has some sort of tuner to receive RF-modulated video signals.\n");
        if(cap.capabilities & V4L2_CAP_AUDIO) printf("The device has audio inputs or outputs.\n");
        if(cap.capabilities & V4L2_CAP_RADIO) printf("This is a radio receiver.\n");
        if(cap.capabilities & V4L2_CAP_MODULATOR) printf("The device has some sort of modulator to emit RF-modulated video/audio signals.\n");
        if(cap.capabilities & V4L2_CAP_SDR_CAPTURE) printf("The device supports the SDR Capture interface.\n");
        if(cap.capabilities & V4L2_CAP_EXT_PIX_FORMAT) printf("The device supports the struct v4l2_pix_format extended fields.\n");
        if(cap.capabilities & V4L2_CAP_SDR_OUTPUT) printf("The device supports the SDR Output interface.\n");
        if(cap.capabilities & V4L2_CAP_META_CAPTURE) printf("The device supports the Metadata Interface capture interface.\n");


        if(cap.capabilities & V4L2_CAP_READWRITE) printf("The device supports the read() and/or write() I/O methods.\n");
        if(cap.capabilities & V4L2_CAP_ASYNCIO) printf("The device supports the asynchronous I/O methods.\n");
        if(cap.capabilities & V4L2_CAP_STREAMING) printf("The device supports the streaming I/O method.\n");
        if(cap.capabilities & V4L2_CAP_META_OUTPUT) printf("The device supports the Metadata Interface output interface.\n");
        if(cap.capabilities & V4L2_CAP_TOUCH) printf("This is a touch device.\n");

        if(cap.capabilities & V4L2_CAP_DEVICE_CAPS) {

            printf("The driver fills the device_caps field.\n");

        }

    }

    return fid_l;
}

/// ****************************************************************
/// ************ ENUMERATE VIDEO AND AUDIO INPUT/OUTPUT ************
/// ****************************************************************

/* Lista os padrões de vídeo suportados pelo hardware (ex: PAL, NTSC) */
int enumerate_video_standards(int fid_l) {

    int idx = 0;
    struct v4l2_standard        stdvid;
    printf("Enumerate supported video standards.\n");
    memset(&stdvid, 0, sizeof(stdvid));
    stdvid.index = idx;
    while(xioctl(fid_l, VIDIOC_ENUMSTD, &stdvid) != -1) {

        printf("\tIndex:\t%d.\n",stdvid.index);
        printf("\tIdentification:\t%d - It needs more details.\n", stdvid.id);
        printf("\tName:\t%s.\n",stdvid.name);
        printf("\tFrame period:\t%d/%d.\n",stdvid.frameperiod.numerator,stdvid.frameperiod.denominator);
        printf("\tLines per frame:\t%d.\n",stdvid.framelines);

        idx++;
        stdvid.index = idx;

    }

    if(!idx) printf("\tThere is no another video standards\n");

    return fid_l;
}

/* Imprime as entradas de áudio e suas propriedades (ex: estéreo) da câmera */
int enumerate_audio_input(int fid_l) {
    /// VIDIOC_G_AUDIO
    /// VIDIOC_S_AUDIO
    /// VIDIOC_ENUMAUDIO
    int idx = 0;
    struct v4l2_audio           Iaudio;     /// audio input

    printf("Enumerate Audio inputs:\n");
    Iaudio.index = idx;
    while(xioctl(fid_l, VIDIOC_ENUMAUDIO, &Iaudio) != -1) {

        printf("\tIndex:\t%d.\n",Iaudio.index);
        printf("\tName:\t%s.\n",Iaudio.name);
        if(Iaudio.capability & V4L2_AUDCAP_STEREO) printf("This is a stereo input.\n");
        if(Iaudio.capability & V4L2_AUDCAP_AVL) printf("Automatic Volume Level mode is supported.\n");
        if(Iaudio.mode == V4L2_AUDMODE_AVL) printf("\tAVL mode is on.\n");
        idx++;
        Iaudio.index = idx;

    }

    if(!idx) printf("\tThere is no audio input\n");

    return fid_l;

}

/* Imprime as saídas de áudio físicas mapeadas pelo driver do dispositivo */
int enumerate_audio_output(int fid_l) {
    /// VIDIOC_G_AUDOUT
    /// VIDIOC_S_AUDOUT
    /// VIDIOC_ENUMAUDOUT
    int idx = 0;
    struct v4l2_audioout        Oaudio;     /// audio output

    printf("Enumerate Audio outputs:\n");
    Oaudio.index = idx;
    while(xioctl(fid_l, VIDIOC_ENUMAUDIO, &Oaudio) != -1) {

        printf("\tIndex:\t%d.\n",Oaudio.index);
        printf("\tName:\t%s.\n",Oaudio.name);

        if(Oaudio.capability & 0) printf("\n");
        if(Oaudio.capability & 0) printf("\n");

        //if(Iaudio.mode == 0) printf("\n");

        idx++;
        Oaudio.index = idx;

    }

    if(!idx) printf("\tThere is no audio output\n");

    return fid_l;
}

/* Lista as conexões físicas de vídeo suportadas (ex: câmera analógica, TV tuner) */
int enumerate_video_input(int fid_l){

    /// VIDIOC_G_INPUT
    /// VIDIOC_S_INPUT
    /// VIDIOC_ENUMINPUT
    int idx = 0;
    struct v4l2_input           iput;       /// video input

    printf("Enumerate Video inputs:\n");
    iput.index = idx;
    while(xioctl(fid_l, VIDIOC_ENUMINPUT, &iput) != -1) {

        printf("\tIndex:\t%d.\n",iput.index);
        printf("\tName:\t%s.\n",iput.name);

        if(iput.type == V4L2_INPUT_TYPE_TUNER) {

            printf("\tThis input uses a tuner (RF demodulator).\n");
            printf("\t\tTuner index: %d", iput.tuner);
            printf("Falta descrever o TUNER\n");

        }

        if(iput.type == V4L2_INPUT_TYPE_CAMERA) printf("\tAnalog baseband input, for example CVBS / Composite Video, S-Video, RGB.\n");
        if(iput.type == V4L2_INPUT_TYPE_TOUCH) printf("\tThis input is a touch device for capturing raw touch data.\n");

        if(iput.audioset != 0) {

            enumerate_audio_input(fid_l);

        }

        /// input.tuner trated in iput.type == V4L2_INPUT_TYPE_TUNER

        if(iput.std != 0) {

           enumerate_video_standards(fid_l);

        }

        if(iput.status != 0) {

            if(iput.status & V4L2_IN_ST_NO_POWER) printf("Attached device is off.\n");
            if(iput.status & V4L2_IN_ST_NO_SIGNAL) printf("No signal.\n");
            if(iput.status & V4L2_IN_ST_NO_COLOR) printf("The hardware supports color decoding, but does not detect color modulation in the signal.\n");
            if(iput.status & V4L2_IN_ST_HFLIP) printf("The input is connected to a device that produces a signal that is flipped horizontally and does not correct this before passing the signal to userspace.\n");
            if(iput.status & V4L2_IN_ST_VFLIP) printf("The input is connected to a device that produces a signal that is flipped vertically and does not correct this before passing the signal to userspace.\n");
            if(iput.status & V4L2_IN_ST_NO_H_LOCK) printf("No horizontal sync lock.\n");
            if(iput.status & V4L2_IN_ST_COLOR_KILL) printf("A color killer circuit automatically disables color decoding when it detects no color modulation. When this flag is set the color killer is enabled and has shut off color decoding.\n");
            if(iput.status & V4L2_IN_ST_NO_V_LOCK) printf("No vertical sync lock.\n");
            if(iput.status & V4L2_IN_ST_NO_STD_LOCK) printf("No standard format lock in case of auto-detection format by the component.\n");
            if(iput.status & V4L2_IN_ST_NO_SYNC) printf("No synchronization lock.\n");
            if(iput.status & V4L2_IN_ST_NO_EQU) printf("No equalizer lock.\n");
            if(iput.status & V4L2_IN_ST_NO_CARRIER) printf("Carrier recovery failed.\n");
            if(iput.status & V4L2_IN_ST_MACROVISION) printf("When this flag is set Macrovision has been detected.\n");
            if(iput.status & V4L2_IN_ST_NO_ACCESS) printf("Conditional access denied.\n");
            if(iput.status & V4L2_IN_ST_VTR) printf("VTR time constant.\n");

        }

        if(iput.capabilities & V4L2_IN_CAP_DV_TIMINGS) printf("This input supports setting video timings by using VIDIOC_S_DV_TIMINGS.\n");
        if(iput.capabilities & V4L2_IN_CAP_STD) printf("This input supports setting the TV standard by using VIDIOC_S_STD.\n");
        if(iput.capabilities & V4L2_IN_CAP_NATIVE_SIZE) printf("This input supports setting the native size using the V4L2_SEL_TGT_NATIVE_SIZE selection target.\n");
        idx++;
        iput.index = idx;

    }

    if(!idx) printf("\tThere is no video input\n");

    return fid_l;
}

/* Lista as conexões físicas de saída de vídeo disponíveis no driver */
int enumerate_video_output(int fid_l) {

    ///VIDIOC_G_OUTPUT
    ///VIDIOC_S_OUTPUT
    ///VIDIOC_ENUMOUTPUT
    int idx = 0;
    struct v4l2_output          oput;       /// video output

    printf("Enumerate Video outputs:\n");
    oput.index = idx;
    while(xioctl(fid_l, VIDIOC_ENUMOUTPUT, &oput) != -1) {

        printf("\tIndex:\t%d.\n",oput.index);
        printf("\tName:\t%s.\n",oput.name);
        if(oput.type == V4L2_OUTPUT_TYPE_MODULATOR) {

            printf("\tThis output is an analog TV modulator.\n");
            printf("\t\tModulator index: %d", oput.modulator);
            printf("Falta descrever o MODULATOR\n");

        }

        if(oput.type == V4L2_OUTPUT_TYPE_ANALOG) printf("\tAnalog baseband output, for example Composite / CVBS, S-Video, RGB.\n");
        if(oput.type == V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY) printf("\tThe video output will be copied to a video overlay.\n");

        if(oput.audioset != 0) {

            enumerate_audio_output(fid_l);

        }

        if(oput.std != 0) {

            enumerate_video_standards(fid_l);

        }

        if(oput.capabilities & V4L2_OUT_CAP_DV_TIMINGS) printf("This output supports setting video timings by using VIDIOC_S_DV_TIMINGS.\n");
        if(oput.capabilities & V4L2_OUT_CAP_STD) printf("This output supports setting the TV standard by using VIDIOC_S_STD.\n");
        if(oput.capabilities & V4L2_OUT_CAP_NATIVE_SIZE) printf("This output supports setting the native size using the V4L2_SEL_TGT_NATIVE_SIZE selection target.\n");

        idx++;
        oput.index = idx;
    }

    if(!idx) printf("\tThere is no video output\n");

    return fid_l;
}

/* Agrupa e dispara a listagem sequencial de entradas e saídas físicas de vídeo */
int list_video_io(int fid_l){

    enumerate_video_input(fid_l);
    enumerate_video_output(fid_l);
    return fid_l;

}

/// ******************************************************
/// ************ ENUMERATE CONTROLS AND MENUS ************
/// ******************************************************

/* Lista os sub-itens e strings textuais de um controle do tipo MENU */
void enumerate_menu(struct v4l2_query_ext_ctrl *qXctrl, int fid_l){

    struct v4l2_querymenu       qmenu;
    printf("\t  Menu items:\n");
    memset(&qmenu, 0, sizeof(qmenu));
    qmenu.id = qXctrl->id;
    for (qmenu.index = qXctrl->minimum; qmenu.index <= qXctrl->maximum; qmenu.index++){
        if (0 == xioctl(fid_l, VIDIOC_QUERYMENU, &qmenu)) {
            printf("\t    %s\n", qmenu.name);
        }
    }
}

/* Varre exaustivamente e exibe todos os controles ajustáveis (brilho, saturação) */
int enumerate_controls(int fid_l){

    struct v4l2_queryctrl       qctrl;
    struct v4l2_query_ext_ctrl  qXctrl;

    printf("Enumerate Controls:\n");

    memset(&qXctrl, 0, sizeof(qXctrl));
    qXctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;

    while (0 == xioctl(fid_l, VIDIOC_QUERY_EXT_CTRL, &qXctrl)) {

        if (!(qXctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {

            printf("\t%s, Max = %lli, Min = %lli, Step = %llu, Dims = %d\n", qXctrl.name, qXctrl.maximum, qXctrl.minimum, qXctrl.step, qXctrl.nr_of_dims);
            if (qXctrl.type == V4L2_CTRL_TYPE_MENU) {

                enumerate_menu(&qXctrl, fid_l);
            }

        }

        qXctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;

    }

    if (errno != EINVAL) {

        perror("VIDIOC_QUERY_EXT_CTRL");
        exit(EXIT_FAILURE);

    }

    memset(&qctrl, 0, sizeof(qctrl));

    for (qctrl.id = V4L2_CID_PRIVATE_BASE; ; qctrl.id++) {

        if (0 == xioctl(fid_l, VIDIOC_QUERYCTRL, &qctrl)) {

            if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED) continue;
            printf("\t%s, Max = %d, Min = %d, Step = %d\n", qctrl.name, qctrl.maximum, qctrl.minimum, qctrl.step);
            if (qctrl.type == V4L2_CTRL_TYPE_MENU) {

                ///enumerate_menu( (struct v4l2_query_ext_ctrl*) qctrl.id, fid_l);
                enumerate_menu(qctrl.id, fid_l);

            }

        } else {

            if (errno == EINVAL) break;
            perror("VIDIOC_QUERYCTRL");
            exit(EXIT_FAILURE);

        }

    }

    return fid_l;

}

/// *********************************************************************************
/// ************ ENUMERATE IMAGE FORMATS, FRAME SIZE AND FRAME INTERVALS ************
/// *********************************************************************************

/* Exibe na tela os intervalos de quadro disponíveis para uma dimensão específica */
int choose_or_enumerate_frame_intervals(int fid_l, struct v4l2_frmivalenum* flg, int expanded) {

    struct v4l2_frmivalenum     fl;
    char tab[4];

    if(!expanded){

        printf(" Available frame intervals for %dx%d frame size:\n", flg->width, flg->height);

    }

    memset(&fl, 0, sizeof(fl));
    fl.pixel_format = flg->pixel_format;
    fl.width = flg->width;
    fl.height = flg->height;

    if(expanded){
        tab[0] = 0;
    }else{
        tab[0] = '\t';
        tab[1] = 0;
    }

    printf("%s", tab);

    while (0 == xioctl(fid_l, VIDIOC_ENUM_FRAMEINTERVALS, &fl) && !(fl.type == V4L2_FRMIVAL_TYPE_CONTINUOUS || fl.type == V4L2_FRMIVAL_TYPE_STEPWISE))  {

        switch(fl.type){

            case V4L2_FRMIVAL_TYPE_DISCRETE:
                printf("%d/%d\t", fl.discrete.numerator, fl.discrete.denominator);
                break;
            case V4L2_FRMIVAL_TYPE_CONTINUOUS:
                printf("\t  continuous: Min: %d/%d, Max: %d/%d, Step: %d/%d\n", fl.stepwise.min.numerator,fl.stepwise.min.denominator,fl.stepwise.max.numerator,fl.stepwise.max.denominator,fl.stepwise.step.numerator,fl.stepwise.step.denominator);
                break;
            case V4L2_FRMIVAL_TYPE_STEPWISE:
                printf("\t  stepwise: Min: %d/%d, Max: %d/%d, Step: %d/%d\n", fl.stepwise.min.numerator,fl.stepwise.min.denominator,fl.stepwise.max.numerator,fl.stepwise.max.denominator,fl.stepwise.step.numerator,fl.stepwise.step.denominator);
                break;
        }

        fl.index++;
        fl.pixel_format = flg->pixel_format;
        fl.width = flg->width;
        fl.height = flg->height;

    }

    printf("\n");

    if(!expanded){
        printf("\t");
        int tmp = 0;
        while(tmp < fl.index){
            printf("%d\t", tmp++);
        }
        printf("\n");
    }

    if(!expanded){
        switch(fl.type){
            case V4L2_FRMIVAL_TYPE_DISCRETE:
                fl.pixel_format = flg->pixel_format;
                fl.width = flg->width;
                fl.height = flg->height;
                fl.index = choose_value(0, fl.index - 1, 1, "Interval index");
                if(0 == xioctl(fid_l, VIDIOC_ENUM_FRAMEINTERVALS, &fl)){
                    flg->discrete.numerator = fl.discrete.numerator;
                    flg->discrete.denominator = fl.discrete.denominator;
                }else{
                    errno_exit("VIDIOC_ENUM_FRAMEINTERVALS");
                }
                break;
            case V4L2_FRMIVAL_TYPE_CONTINUOUS:
            case V4L2_FRMSIZE_TYPE_STEPWISE:
                flg->discrete.numerator = choose_value(fl.stepwise.min.numerator, fl.stepwise.max.numerator, fl.stepwise.step.numerator,"Numerator");
                flg->discrete.denominator = choose_value(fl.stepwise.min.denominator, fl.stepwise.max.denominator, fl.stepwise.step.denominator,"Denominator");
                break;
        }
    }

    return fid_l;

}

/* Exibe na tela as resoluções discretas ou contínuas suportadas por um formato */
int choose_or_enumerate_frame_size(int fid_l, struct v4l2_frmivalenum* fl, int expanded) {

    struct v4l2_frmsizeenum      fz;
    char num[4];
    char p[5] = {0,0,0,0,0};


    if(!expanded){

        *((int*)p) = fl->pixel_format;
        printf(" Available frame sizes for %s format:\n", p);

    }

    memset(&fz, 0, sizeof(fz));
    fz.pixel_format = fl->pixel_format;

    while (0 == xioctl(fid_l, VIDIOC_ENUM_FRAMESIZES, &fz) && !(fz.type == V4L2_FRMSIZE_TYPE_CONTINUOUS || fz.type == V4L2_FRMSIZE_TYPE_STEPWISE)) {

        if(!expanded){
            sprintf(num,"%d:",fz.index);
        }else{
            num[0]=0;
        }

        switch(fz.type){

            case V4L2_FRMSIZE_TYPE_DISCRETE:
                printf("\t  %s %dx%d\t", num, fz.discrete.width, fz.discrete.height);
                break;
            case V4L2_FRMSIZE_TYPE_CONTINUOUS:
                printf("\t  continuous: Min-W: %d, Max-W: %d, Step-W: %d, Min-H: %d, Max-H: %d, Step-H: %d\n", fz.stepwise.min_width,fz.stepwise.max_width, fz.stepwise.step_width, fz.stepwise.min_height, fz.stepwise.max_height, fz.stepwise.step_height);
                break;
            case V4L2_FRMIVAL_TYPE_STEPWISE:
                printf("\t  stepwise: Min-W: %d, Max-W: %d, Step-W: %d, Min-H: %d, Max-H: %d, Step-H: %d\n", fz.stepwise.min_width,fz.stepwise.max_width, fz.stepwise.step_width, fz.stepwise.min_height, fz.stepwise.max_height, fz.stepwise.step_height);
                break;

        }

        if(expanded){
            fl->width = fz.discrete.width;
            fl->height = fz.discrete.height;
            choose_or_enumerate_frame_intervals(fid_l, fl, expanded);

        }else{

            printf("\n");

        }

        fz.index++;
        fz.pixel_format = fl->pixel_format;

    }

    if(!expanded){
        switch(fz.type){
            case V4L2_FRMSIZE_TYPE_DISCRETE:
                fz.pixel_format = fl->pixel_format;
                fz.index = choose_value(0, fz.index - 1, 1, "size index");
                if(0 == xioctl(fid_l, VIDIOC_ENUM_FRAMESIZES, &fz)){
                    fl->width = fz.discrete.width;
                    fl->height = fz.discrete.height;
                }else{
                    errno_exit("VIDIOC_ENUM_FRAMESIZES");
                }
                break;
            case V4L2_FRMSIZE_TYPE_CONTINUOUS:
            case V4L2_FRMSIZE_TYPE_STEPWISE:
                fl->width = choose_value(fz.stepwise.min_width, fz.stepwise.max_width, fz.stepwise.step_width,"Width");
                fl->height = choose_value(fz.stepwise.min_height, fz.stepwise.max_height, fz.stepwise.step_height,"Height");
                break;
        }

    }


    return fid_l;

}

/* Exibe os formatos de imagem comprimidos ou nativos aceitos pela câmera */
int choose_or_enumerate_image_formats(int fid_l, struct v4l2_frmivalenum* fl, int expanded) {

    char num[4];
    char p[5] = {0,0,0,0,0};
    struct v4l2_fmtdesc         fd;


    memset(&fd, 0, sizeof(fd));
    fd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    while (0 == xioctl(fid_l, VIDIOC_ENUM_FMT, &fd)) {

        *((int*)p) = fd.pixelformat;

        if(!expanded){
            sprintf(num,"%d:",fd.index);
        }else{
            num[0]=0;
            fl->pixel_format = fd.pixelformat;
        }

        printf("  %s %s - %s\n", num , p, fd.description);

        if(expanded){

            if(fd.flags & V4L2_FMT_FLAG_COMPRESSED) printf("     This is a compressed format.\n");
            if(fd.flags & V4L2_FMT_FLAG_EMULATED) printf("     This format is not native to the device but emulated through software.\n");
            if(fd.flags & V4L2_FMT_FLAG_CONTINUOUS_BYTESTREAM) printf("     The hardware decoder for this compressed bytestream format (aka coded format) is capable of parsing a continuous bytestream.\n");
            if(fd.flags & V4L2_FMT_FLAG_DYN_RESOLUTION) printf("     Dynamic resolution switching is supported by the device for this compressed bytestream format.\n");

            choose_or_enumerate_frame_size(fid_l, fl, expanded);

        }

        fd.index++;
        fd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    }

    if(!expanded){
        fd.index = choose_value(0, fd.index - 1, 1, "Format index");
        fd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if(0 == xioctl(fid_l, VIDIOC_ENUM_FMT, &fd)){
            fl->pixel_format = fd.pixelformat;
        }else{
            errno_exit("VIDIOC_ENUM_FMT");
        }
    }


    return fid_l;

}


/* Processa o buffer, calcula o redimensionamento ideal e desenha a imagem em ASCII art */
/*
void asciiart(const void *data, int isize, int ih, int iw){

/// ' .,;!vlLFE$'
/// '$EFLlv!;,. '
/// ' .:-=+*#%@'
/// '@%#*+=-:. '
/// '$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\|()1{}[]?-_+~<>i!lI;:,"^`'. '
/// '@MBHENR#KWXDFPQASUZbdehx*8Gm&04LOVYkpq5Tagns69owz$CIu23Jcfry%1v7l+it[] {}?j|()=~!-/<>\"^_';,:`. '
/// unsigned char map[] = "@MBHENR#KWXDFPQASUZbdehx*8Gm&04LOVYkpq5Tagns69owz$CIu23Jcfry%1v7l+it[]{}?j|()=~!-/<>\"^_';,:`.                                                                                                                                                                   ";
/// unsigned char map[] = "@MBHENR#KWXDFPQASUZbdehx*8Gm&04LOVYkpq5Tagns69owz$CIu23Jcfry81v7l+it[]{}?j|()=~!-i<>i^_';,:`....................................................................................................................................................................";
/// unsigned char map[] = "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM";
  unsigned char mapchar[] = "                               .........................-------------------------:::::::::::::::::::::::::=========================+++++++++++++++++++++++++*************************#########################$$$$$$$$$$$$$$$$$$$$$$$$$@@@@@@@@@@@@@@@@@@@@@@@@@";


  char imx[150000];
  char* pc;
  //int sz = 0;
  short int* p;
  unsigned char* q;
  int i,j, l;
  int rzH;
  int rzW;
  int ct = 0;

#define HDOT 16
#define WDOT  8
#define NCH  20

  update_terminal_size();
  SCRCLR();


  char title[] = "IoT - Image on Terminal";
  GOTOXY(1,(maxTW-strlen(title))/2);
  printf("%s",title);

  int nch = maxTH-4; ///NCH;
  int ncw = (iw * nch * HDOT)/(ih * WDOT);


  if(ncw > maxTW-4){
    ncw = maxTW-4;
    nch = (ncw * ih * WDOT)/(iw * HDOT);
  }

  rzH = ih/nch;
  rzW = iw/ncw;

  p = (short int*)data;
  pc = imx;

  if(isize == ih*iw){
    /// 8-bit gray image format

  }else if(isize == 3*ih*iw){
    /// 24-bit rgb image format

  }else{
    /// other format (YUYV)
    for(i=0;i<nch;i++){
      for(j=0;j<ncw;j++){
        q = (unsigned char*)(p + (i * rzH) * iw + j * rzW);
        pc[0] = mapchar[q[0]];
        pc++;
        ct++;
        if(ct == ncw){
          ct = 0;
          for(l=0;l<(maxTW-ncw);l++){
            pc[0] = ' ';
            pc++;
          }
        }
      }
    }
  }
  pc[0] = 0;

  printf("\e[?25l"); /// Hide the cursor

  GOTOXY(3,((maxTW-ncw-4)/2)+2);
  printf("%s",imx);

  printf("\e[?25h"); /// show the cursor

  fr++;
  gettimeofday(&t2, NULL);
  if((t2.tv_sec*1000 + t2.tv_usec/1000) - (t1.tv_sec*1000 + t1.tv_usec/1000) > 1000){
    GOTOXY(maxTH,1);
    printf("Frame Rate: %d",fr-1);
	fr = 0;
	t1 = t2;
  }
}

*/


/*
 * Renderiza um buffer universal de brilho de 8 bits (Grayscale) em formato de texto.
 * Aceita estritamente matrizes contíguas de 1 byte por pixel geradas pela convert_to_grayscale.
 */
void asciiart(const void *data, int isize, int ih, int iw) {
    // Escala de tons de cinza do projeto (do preto ao branco total)
    unsigned char mapchar[] = "                               "
                              "........................."
                              "-------------------------"
                              ":::::::::::::::::::::::::"
                              "========================="
                              "+++++++++++++++++++++++++"
                              "*************************"
                              "#########################"
                              "$$$$$$$$$$$$$$$$$$$$$$$$$"
                              "@@@@@@@@@@@@@@@@@@@@@@@@@";

    static char imx[150000];
    char* pc;
    const unsigned char* q; // Ponteiro de leitura linear direta de 8 bits
    int i, j, l;
    int rzH;
    int rzW;
    int ct = 0;

#define HDOT 16
#define WDOT  8

    // Atualiza as dimensões físicas atuais da janela do terminal (maxTH e maxTW)
    update_terminal_size();
    SCRCLR();

    // Imprime o título centralizado no topo do terminal
    char title[] = "IoT - Image on Terminal (Universal Grayscale)";
    GOTOXY(1, (maxTW - strlen(title)) / 2);
    printf("%s", title);

    // Calcula a proporção de redimensionamento ideal baseada nas fontes de texto
    int nch = maxTH - 4;
    int ncw = (iw * nch * HDOT) / (ih * WDOT);

    if (ncw > maxTW - 4) {
        ncw = maxTW - 4;
        nch = (ncw * ih * WDOT) / (iw * HDOT);
    }

    rzH = ih / nch;
    rzW = iw / ncw;

    // Configura os ponteiros para leitura e escrita linear na string de exibição
    q = (const unsigned char*)data;
    pc = imx;

    // Garante proteção caso as dimensões de amostragem fiquem zeradas
    if (rzH <= 0) rzH = 1;
    if (rzW <= 0) rzW = 1;

    /*
     * LAÇO CORE REMODELADO:
     * Leitura direta e universal de 1 byte por pixel, independente do hardware da câmera.
     */
    for (i = 0; i < nch; i++) {
        for (j = 0; j < ncw; j++) {

            // Localiza o pixel correto na matriz contígua baseando-se no salto de escala
            unsigned long pixel_pos = (unsigned long)(i * rzH) * iw + (j * rzW);

            // Proteção simples de estouro de leitura de segurança
            if (pixel_pos < (unsigned long)isize) {
                pc[0] = mapchar[q[pixel_pos]];
            } else {
                pc[0] = ' ';
            }
            pc++;
            ct++;

            // Trata as quebras de linha e preenche o espaço restante da tela
            if (ct == ncw) {
                ct = 0;
                for (l = 0; l < (maxTW - ncw); l++) {
                    pc[0] = ' ';
                    pc++;
                }
            }
        }
    }
    pc[0] = '\0'; // Finaliza a string de caracteres

    printf("\e[?25l"); // Oculta o cursor do terminal

    // Desenha a imagem processada centralizada horizontalmente na linha 3
    GOTOXY(3, ((maxTW - ncw - 4) / 2) + 2);
    printf("%s", imx);

    printf("\e[?25h"); // Exibe o cursor de volta

    // Cálculo e atualização em tempo real do frame rate na última linha
    fr++;
    gettimeofday(&t2, NULL);
    if ((t2.tv_sec * 1000 + t2.tv_usec / 1000) - (t1.tv_sec * 1000 + t1.tv_usec / 1000) > 1000) {
        GOTOXY(maxTH, 1);
        printf("Frame Rate: %d", fr - 1);
        fr = 0;
        t1 = t2;
    }
}






/* Exibe a listagem filtrada de dispositivos passados em um vetor de v4l2_id */
static void show_device_list(v4l2_id *dv, int show_num) { /// show_num is the number of devices to be shown. It must be smaller than dv vector size.

    int i;
    char nameofdevice[256];
    int fd = -1;
    int err;
    struct v4l2_capability vcap;


	for(i =  0; i< show_num; i++) {

        sprintf(nameofdevice, "/dev/%s%d",prefixes[dv[i].typ],dv[i].num);
        fd = open(nameofdevice, O_RDWR  | O_NONBLOCK, 0);
        if (fd < 0) continue;
        err = xioctl(fd, VIDIOC_QUERYCAP, &vcap);
        close(fd);
        if (err) continue;
        printf("  %d: Type %s: %s, %s, %s\n", i, prefixes[dv[i].typ], nameofdevice, vcap.card, vcap.driver);

	}

}

/* Oferece uma interface no terminal para o usuário digitar o índice da câmera desejada */
static void select_v4l2_device(char *dv_nm, v4l2_id *dv, int found_num) {

    int idx = INT_MAX;
    dv_nm[0] = 0;

    if(!found_num) return;

    if(found_num == 1){

        idx = 0;

	}else if(found_num > 1) {

        while(idx >= found_num){

            printf("\n\tChoose device number and type ENTER: ");
            scanf("%d",&idx);
            printf("\n\n");

        }

    }

    get_v4l2_path(dv_nm,idx);

    if(!dv_nm[0]) {

        printf("Device not exist\n");
        exit(EXIT_FAILURE);

    }

}

/* Menu interativo genérico de leitura numérica para capturar a escolha do usuário */
int choose_value(int minv, int maxv, int step, char* tp) {

    int idx = -1;
    if(minv == maxv){
        return minv;
    }
    while(idx < minv || idx > maxv || 0 != (idx % step)){

        idx = -1;

        printf("\tChoose a %s value and type ENTER: ", tp);
        scanf("%d",&idx);

    }
    return idx;

}

/* Descomprime o buffer MJPEG e extrai apenas o canal de brilho (Grayscale 8-bit) */
unsigned char* mjpeg_to_grayscale(const unsigned char* jpeg_buffer, unsigned long jpeg_size, int *out_h, int *out_w) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    // Configura o buffer de memória de entrada
    jpeg_mem_src(&cinfo, jpeg_buffer, jpeg_size);

    // Lê os metadados do cabeçalho JPEG para descobrir a resolução nativa
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    // ARQUITETURA DE BAIXO NÍVEL: Força o decodificador a extrair apenas Tons de Cinza.
    // Isso reduz drasticamente o consumo de memória RAM e processamento no laço.
    cinfo.out_color_space = JCS_GRAYSCALE;

    jpeg_start_decompress(&cinfo);

    *out_h = cinfo.output_height;
    *out_w = cinfo.output_width;

    unsigned long gray_size = (*out_h) * (*out_w);
    unsigned char* gray_buffer = (unsigned char*) malloc(gray_size);

    if (!gray_buffer) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return NULL;
    }

    // Descompacta e lê o frame linha por linha salvando diretamente no buffer
    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char* row_pointer;
        row_pointer = &gray_buffer[cinfo.output_scanline * cinfo.output_width];
        // Correção de sintaxe: passa-se o endereço do ponteiro da linha (&row_pointer)
        jpeg_read_scanlines(&cinfo, &row_pointer, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return gray_buffer; // Retorna um mapa contíguo de brilho de 8 bits por pixel
}

/* Extrai o canal de brilho (Y) pulando os bytes de croma (U/V) no formato intercalado */
unsigned char* extract_yuyv_brightness(const unsigned char* raw_buffer, unsigned long buffer_size) {
    unsigned long gray_size = h * w;
    unsigned char* gray_buffer = (unsigned char*) malloc(gray_size);

    if (!gray_buffer) return NULL;

    unsigned long i, j = 0;
    // raw_buffer avança de 2 em 2 bytes (Y0, deoc, Y1, deoc...)
    for (i = 0; i < buffer_size && j < gray_size; i += 2) {
        gray_buffer[j++] = raw_buffer[i];
    }

    return gray_buffer;
}

/* Copia diretamente o plano Y contíguo que já vem isolado no início do buffer planar */
unsigned char* extract_planar_brightness(const unsigned char* raw_buffer, unsigned long buffer_size) {
    unsigned long gray_size = h * w;
    unsigned char* gray_buffer = (unsigned char*) malloc(gray_size);

    if (!gray_buffer) return NULL;

    // Como o plano Y ocupa exatamente a primeira parte do buffer (1 byte por pixel),
    // realizamos um dump direto usando memcpy de alta velocidade.
    memcpy(gray_buffer, raw_buffer, gray_size);

    return gray_buffer;
}

/* Converte pixels de cor RGB/BGR de 3 bytes para brilho de 1 byte usando shift de bits */
unsigned char* rgb_to_grayscale(const unsigned char* raw_buffer, unsigned long buffer_size) {
    unsigned long gray_size = h * w;
    unsigned char* gray_buffer = (unsigned char*) malloc(gray_size);

    if (!gray_buffer) return NULL;

    unsigned long i = 0, j = 0;
    while (i < buffer_size && j < gray_size) {
        unsigned char r, g, b;

        if (pxfmt == V4L2_PIX_FMT_RGB24) {
            r = raw_buffer[i];
            g = raw_buffer[i+1];
            b = raw_buffer[i+2];
        } else { // V4L2_PIX_FMT_BGR24
            b = raw_buffer[i];
            g = raw_buffer[i+1];
            r = raw_buffer[i+2];
        }

        // Aritmética rápida por aproximação de inteiros fixos (escala 256) equivalente à luminância
        gray_buffer[j++] = (unsigned char)((r * 77 + g * 150 + b * 29) >> 8);
        i += 3; // Salta para o próximo pixel de 3 bytes
    }

    return gray_buffer;
}

/*
 * Encapsula todo o chaveamento de decodificação.
 * Retorna SEMPRE um buffer padronizado em Grayscale 8-bit (1 byte por pixel).
 */
unsigned char* convert_to_grayscale(const unsigned char* raw_buffer, unsigned long buffer_size) {

    switch (pxfmt) {

        // Categoria 1: Formatos Comprimidos (Exigem descompressão via bibliotecas externas C)
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
            return mjpeg_to_grayscale(raw_buffer, buffer_size, &h, &w);

        // Categoria 2: Formatos Cruizados Intercalados (Apenas saltos de ponteiro na memória)
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
            return extract_yuyv_brightness(raw_buffer, buffer_size);

        // Categoria 3: Formatos Planares (Onde o canal Y de brilho já vem isolado no início do buffer)
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_YUV420:
            return extract_planar_brightness(raw_buffer, buffer_size);

        // Categoria 4: Formatos de Cor Direta (Exigem cálculo matemático de conversão RGB -> Gray)
        case V4L2_PIX_FMT_RGB24:
        case V4L2_PIX_FMT_BGR24:
            return rgb_to_grayscale(raw_buffer, buffer_size);

        // Caso o driver use um formato exótico não tratado, avisa o usuário e aborta graciosamente
        default:
            fprintf(stderr, "Formato FourCC '%4s' não suportado em modo texto.\n", (char*)&pxfmt);
            return NULL;
    }
}




/*
 * Popula a tabela estática global apenas com os controles que são REALMENTE ajustáveis.
 * Filtra e descarta controles somente leitura (Min == Max) ou inválidos (Step == 0 para inteiros).
 */
int populate_controls_table(int fid_l) {
    struct v4l2_query_ext_ctrl qXctrl;
    struct v4l2_control ctrl_get;

    num_controls_found = 0;
    memset(camera_ctrl_table, 0, sizeof(camera_ctrl_table));

    memset(&qXctrl, 0, sizeof(qXctrl));
    qXctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;

    while (0 == xioctl(fid_l, VIDIOC_QUERY_EXT_CTRL, &qXctrl)) {
        // Ignora controles explicitamente desativados pelo driver
        if (!(qXctrl.flags & V4L2_CTRL_FLAG_DISABLED) && num_controls_found < MAX_CAMERA_CONTROLS) {

            /*
             * FILTRO DE SEGURANÇA E CAPACIDADE DE AJUSTE:
             * 1. Se Min for igual a Max, o controle é estático (Read-Only).
             * 2. Para tipos numéricos comuns (INTEGER, BOOLEAN, MENU), se o Step for 0,
             *    não há como incrementar ou decrementar o valor de forma previsível.
             */
            bool is_read_only = (qXctrl.minimum == qXctrl.maximum);
            bool is_invalid_step = (qXctrl.step == 0 &&
                                   (qXctrl.type == V4L2_CTRL_TYPE_INTEGER ||
                                    qXctrl.type == V4L2_CTRL_TYPE_BOOLEAN ||
                                    qXctrl.type == V4L2_CTRL_TYPE_MENU));

            if (is_read_only || is_invalid_step) {
                // Pula este controle e vai para o próximo da lista do driver
                qXctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
                continue;
            }

            // Se passou no filtro, mapeia o controle ajustável na tabela RAM
            camera_ctrl_table[num_controls_found].id = qXctrl.id;
            camera_ctrl_table[num_controls_found].minimum = qXctrl.minimum;
            camera_ctrl_table[num_controls_found].maximum = qXctrl.maximum;
            camera_ctrl_table[num_controls_found].step = qXctrl.step;
            camera_ctrl_table[num_controls_found].type = qXctrl.type;
            strncpy(camera_ctrl_table[num_controls_found].name, qXctrl.name, 31);
            camera_ctrl_table[num_controls_found].name[31] = '\0'; // Garante terminação nula

            // Captura o valor de estado atual direto do hardware
            memset(&ctrl_get, 0, sizeof(ctrl_get));
            ctrl_get.id = qXctrl.id;
            if (0 == xioctl(fid_l, VIDIOC_G_CTRL, &ctrl_get)) {
                camera_ctrl_table[num_controls_found].current_value = ctrl_get.value;
            } else {
                camera_ctrl_table[num_controls_found].current_value = (int)qXctrl.default_value;
            }

            num_controls_found++;
        }
        qXctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    }

    return num_controls_found;
}


/******************************************************************************
 * SECTION 3: FUNÇÕES GENÉRICAS, COMPATIBILIDADE E SISTEMA (FIM)
 *
 * Objetivo: Prover utilitários globais do sistema de arquivos, checagens de
 * ambiente (GUI/CLI), e pontos de entrada abstratos para varredura de hardware.
 *****************************************************************************/

/* Retorna o ponteiro do buffer estático global que contém o caminho atual (/dev/videoX) */
char* get_dev_name(){

    return dev_name;

}

/* Retorna a estrutura v4l2_id correspondente a um índice da lista de encontrados */
v4l2_id* get_v4l2_id(int idx){

    if(idx >= 0 && idx < num_dev_found){
        return &v4l2_device_list[idx];
    }
    return 0;

}

/* Avalia se um nome de arquivo em /dev casa com os prefixos válidos do subsistema */
static bool is_v4l2_dev(const char *name, enum v4l2_device_type* typ, int* num) {

    unsigned l;

    for(*typ = V4L2_DEV_VIDEO; *typ <= V4L2_DEV_V4L_TOUCH; (*typ)++) {

        l = strlen(prefixes[*typ]);
        if (!memcmp(name, prefixes[*typ], l)) {
			if (isdigit(name[l])){
                sscanf(&name[l], "%d", num);
                return true;
			}
		}

    }
	return false;

}

/* Preenche um vetor de v4l2_id varrendo o diretório /dev baseado no filtro de tipo */
static int get_v4l2_device_list(v4l2_id *list, enum v4l2_device_type dvtp, int sz) {  /// dv_sz is the list vector size.

    int idx = 0;
	DIR *dp;
	char nameofdevice[256];

	struct dirent *ep;
	struct v4l2_capability vcap;
	int fd = -1;
	int err;

	int num;                        /// device node number
	enum v4l2_device_type typ;      /// device node type

	memset(list,-1,sz*sizeof(v4l2_id));

	dp = opendir("/dev");

	if (dp == NULL) {

		perror ("Couldn't open the directory /dev/");
		exit(EXIT_FAILURE);

	}

	while ((ep = readdir(dp)) && idx < sz) {

		if (is_v4l2_dev(ep->d_name, &typ, &num)) {

            if(dvtp == V4L2_DEV_ALL || dvtp == typ){

                nameofdevice[0] = 0;
                strcat(nameofdevice,"/dev/");
                strcat(nameofdevice,ep->d_name);
                fd = open(nameofdevice, O_RDWR  | O_NONBLOCK, 0);
                if (fd < 0) continue;
                err = xioctl(fd, VIDIOC_QUERYCAP, &vcap); /// if there is no error, the device complies with V4L2 specification
                close(fd);
                if (err) continue;

                list[idx].typ = typ;
                list[idx].num = num;

                idx++;

            }

		}

	}

	closedir(dp);
    return idx;

}

/* Ponto de entrada de alto nível que monta a listagem de absolutamente todos os tipos V4L2 */
int make_v4l2_list() {

    num_dev_found =  get_v4l2_device_list(v4l2_device_list, V4L2_DEV_ALL, MAX_V4L2_DEV_NUM);
    return num_dev_found;

}

/* Ponto de entrada focado em carregar e contar apenas dispositivos de vídeo (/dev/video*) */
int make_video_list() {

    num_dev_found =  get_v4l2_device_list(v4l2_device_list, V4L2_DEV_VIDEO, MAX_V4L2_DEV_NUM);
    return num_dev_found;

}

/* Monta uma string de caminho absoluto baseado no tipo e número do node */
void make_v4l2_path(char* dv_ph, enum v4l2_device_type typ, unsigned char num ){

    sprintf(dv_ph, "/dev/%s%d", prefixes[typ],num);

}

/* Obtém o caminho absoluto direto no sistema com base no ID mapeado internamente */
void get_v4l2_path(char* dv_ph, int idx){

    if(!dv_ph) return;
    dv_ph[0] = 0;
    if(idx >= 0 && idx < num_dev_found)
        make_v4l2_path(dv_ph, v4l2_device_list[idx].typ, v4l2_device_list[idx].num);

}

/* Checa se a lista global de dispositivos já foi povoada e a imprime em tela */
void show_v4l2_list(){

    if(num_dev_found){

        printf("\n\nAvailable V4L2 device list:\n");
        show_device_list(v4l2_device_list, num_dev_found);

    }else{

        if(make_v4l2_list()){

            show_v4l2_list();

        }else{

            perror ("There is not V4L2 device to show.");
            exit(EXIT_FAILURE);

        }

    }

}

/* Limpa a tela e força a atualização e exibição da lista de captura de vídeo */
void show_video_list(){

    SCRCLR();

    if(make_video_list()){

        printf("\n\nAvailable video device list:\n");
        show_device_list(v4l2_device_list, num_dev_found);

    }else{

        perror ("There is not video device to show.");
        exit(EXIT_FAILURE);

    }

}


/* Varre as capacidades físicas e retorna qual método de I/O está livre e operacional */
enum io_method check_io_method_busy(int fid_l){

    struct v4l2_requestbuffers req;
    struct v4l2_capability cap;
    int iom;

    if (-1 == xioctl(fid_l, VIDIOC_QUERYCAP, &cap)) {

        if (EINVAL == errno) {

            fprintf(stderr, "No V4L2 device\n");
            exit(EXIT_FAILURE);

        } else {

            errno_exit("VIDIOC_QUERYCAP");

        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)){

        fprintf(stderr, "No video capture device\n");
        exit(EXIT_FAILURE);

    }

    //Use Capabilities to set IO method
    if(cap.capabilities & V4L2_CAP_READWRITE){
        iom = IO_METHOD_READ;
    }

    if(cap.capabilities & V4L2_CAP_STREAMING){

        memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_USERPTR;
        req.count = 1;
        if (-1 == xioctl (fid_l, VIDIOC_REQBUFS, &req)){
            if (errno == EBUSY)
                iom = -1;
            else
                iom = -2;
                ///printf("error in check_io_method_busy function\n");
        }else{
            iom = IO_METHOD_USERPTR;
        }

        req.count = 0;
        if (-1 == xioctl (fid_l, VIDIOC_REQBUFS, &req)){
            if (errno != EBUSY){
                iom = -2;
                ///printf("error in check_io_method_busy function\n");
            }
        }


        memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        req.count = 1;
        if (-1 == xioctl (fid_l, VIDIOC_REQBUFS, &req)){
            if (errno == EBUSY)
                iom = -1;
            else
                iom = -2;
                ///printf("error in check_io_method_busy function\n");
        }else{
            iom = IO_METHOD_MMAP;
        }

        req.count = 0;
        if (-1 == xioctl (fid_l, VIDIOC_REQBUFS, &req)){
            if (errno != EBUSY){
                iom = -2;
                ///printf("error in check_io_method_busy function\n");
            }
        }


        /*
        memset(&req, 0, sizeof(req));
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_DMABUF;
        req.count = 4;
        if (-1 == xioctl (fid_l, VIDIOC_REQBUFS, &req)){
            if (errno == EINVAL)
                printf("Video capturing or mmap-streaming is not supported\\n");
            else
                perror("VIDIOC_REQBUFS");
        }else{
            io = IO_METHOD_DMABUF;
        }
        */
    }

    return iom;

}

/* Verifica a existência da variável de ambiente DISPLAY para saber se há servidor X */
int is_gui_present(){

    if (NULL == getenv("DISPLAY")){
        return false;
    }
    return true;

}

///     Detect GUI or terminal
///     https://stackoverflow.com/questions/13204177/how-to-find-out-if-running-from-terminal-or-gui
///     The standard C way of determining whether X Window is present:
/*
#include <stdlib.h>
if (NULL == getenv("DISPLAY")){
    is_gui_present = false;
}else{
    is_gui_present = true;
}
*/
///    this allows to distinguish pseudo-terminals in terminal emulator and pure tty launch.

///     Detect launched from the command line or launched from inside the desktop
///     Launching programs from the desktop (double click or from a desktop file/start menu) will
///     usually redirect their stdin file descriptor to a pipe. You can detect this:
/*
#include <cstdio>    // fileno()
#include <unistd.h>  // isatty()
if (isatty(fileno(stdin)))
    // We were launched from the command line.
else
    // We were launched from inside the desktop
*/


/* Verifica através de isatty() se o canal stdin está acoplado a um terminal */
int from_command_line(){

    if (isatty(fileno(stdin))){
        return true;
    }
    return false;

}

/* Checa se uma determinada câmera identificada por tipo/número consta na lista ativa */
int found_in_list(v4l2_id vid, char* ck){

    int i = 0;
    while(i < num_dev_found){
        if(vid.typ == v4l2_device_list[i].typ && vid.num == v4l2_device_list[i].num){
            ck[i] = 1;
            break;
        }
        i++;
    };

    if(i == num_dev_found){
        return 0;
    }

    return 1;

}

/* Core unificado de alto nível que roda a sequência de detecção e escolha do dispositivo */
void choose_v4l2_device(){

    show_v4l2_list();

    select_v4l2_device(dev_name, v4l2_device_list, num_dev_found);

}

/* Converte blocos de dados YUYV nativos em buffers RGB24 usando tabelas de busca fixas */
/// ***************************
#define POWER 15
#define pr (1 << POWER)
static const int kY  = (int) 1.164 * pr + 0.5;
static const int kRV = (int) 1.596 * pr + 0.5;
static const int kGU = (int) 0.391 * pr + 0.5;
static const int kGV = (int) 0.813 * pr + 0.5;
static const int kBU = (int) 2.018 * pr + 0.5;

static int coefY[256];
static int coefRV[256];
static int coefGU[256];
static int coefGV[256];
static int coefBU[256];

/* Pré-calcula coeficientes de cor para acelerar a conversão matemática de YUV para RGB */
void fill_lookuptables(){
    int i;
    for(i=0;i<256;i++){
        coefY[i] = kY * (i-16) + (pr/2);
        coefRV[i] = kRV * (i-128);
        coefGU[i] = -kGU * (i-128);
        coefGV[i] = -kGV * (i-128);
        coefBU[i] = kBU * (i-128);
    }
}

void yuv_to_rgb(const void *p_rgb, const void *p_yuv, int isize, int ih, int iw){

    unsigned char* p = (unsigned char*)p_rgb;
    unsigned char* q = (unsigned char*)p_yuv;

    for(int i=0;i<((ih * iw)/2);i++){

        /// First pixel
        p[0] = (coefY[q[0]] +      0       + coefRV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[0]] + coefGU[q[3]] + coefGV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[0]] + coefBU[q[3]] +      0      ) >> POWER;
        p++;
        /// Second pixel
        p[0] = (coefY[q[2]] +      0       + coefRV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[2]] + coefGU[q[3]] + coefGV[q[1]]) >> POWER;
        p++;
        p[0] = (coefY[q[2]] + coefBU[q[3]] +      0      ) >> POWER;
        p++;
        q+=4;
    }
}
/// ***************************


/* Aloca memória no heap do usuário para acomodar buffers de frames RGB manipuláveis */
// image area pointer
unsigned char* v = 0;
/// ***************************
void allocate_image(int h, int w){
    if(v){
        free(v);
        v = 0;
    }
    v = (unsigned char*) malloc(3 * h * w);
}

/* Libera o ponteiro de imagem gerado previamente para evitar vazamentos de memória */
void deallocate_image(){
    if(v){
        free(v);
        v = 0;
    }
}


/******************************************************************************
 * COMPATIBILIDADE DE ARQUITETURA
 *****************************************************************************/

/*
 * Versão padrão (fraca) para evitar erros de compilação quando o icap.c não for usado.
 * Se o icap.c for incluído no projeto, o compilador ignora este bloco vazio e usa
 * a implementação real do icap que renderiza a imagem em ASCII art.
 */
__attribute__((weak)) void process_image(const void *p, int isize) {
    // Fica vazia de propósito para não interferir no camview.cpp
}

