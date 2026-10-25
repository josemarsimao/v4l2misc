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
 *  ---------------------------------------------------------------------------
 *  v4l2misc.c - BIBLIOTECA DE CÂMERA ÚNICA (fachada sobre o núcleo v4l2core)
 *
 *  Etapa 4: este arquivo ficou só com o que é próprio de UMA câmera usada por um
 *  programa de thread única (icap):
 *    - guarda o estado dessa câmera (sessão estática + globais io/w/h/pxfmt);
 *    - define a política de erro desse modo: diagnóstico + exit();
 *    - liga o núcleo ao teclado (_kbhit) e a process_image() (callback);
 *    - converte o frame para cinza usando o módulo v4l2gray.
 *
 *  Foram movidos para módulos próprios (veja v4l2misc.h):
 *    v4l2enum.c (descoberta/enumeração/diagnóstico), v4l2term.c (terminal e ASCII),
 *    v4l2gray.c (conversão de pixels, única que usa libjpeg).
 *  ---------------------------------------------------------------------------
 */

#include "v4l2misc.h"
#include "kbhit.h"

/// ***************************
/// Estado da câmera única
int                 force_format;           /* 1 = força 640x480 YUYV em init_device()                 */
enum io_method      io = -1;                /* método de I/O (-1 = automático)                         */
int                 w = 0;                  /* largura do frame atual                                  */
int                 h = 0;                  /* altura do frame atual                                   */
__u32               pxfmt;                  /* pixel format atual (FOURCC)                             */

/* A sessão do núcleo v4l2core substitui as antigas globais 'buffers' e 'num_buf'.
 * Só esta fachada a acessa (static). */
static v4l2_session sess;
static v4l2ctl_set  ctls;                   /* controles da câmera única (brilho, contraste...), ver load_controls() */
static char         kb_last_key = '.';      /* última tecla lida em mainloop() */
/// ***************************

/*
 * Mensagem fatal para erros ocorridos numa sessão do núcleo (v4l2core).
 *
 * O núcleo apenas devolve códigos de erro; neste modo (câmera única, programa
 * de linha de comando) a política é a original: mostrar o diagnóstico
 * detalhado e encerrar o programa. Quem quiser outra política (ex.: multiview,
 * que não pode morrer por causa de uma câmera) usa o núcleo diretamente.
 */
static void die_session(const v4l2_session *s) {

    errno = s->err_no;                  /* restaura o errno guardado no momento da falha */
    explain_v4l2_error(s->err_ctx);     /* diagnóstico amigável (usa o errno)            */

    errno = s->err_no;                  /* o printf do diagnóstico pode ter alterado errno */
    errno_exit(s->err_ctx);             /* imprime "<contexto> error N, <texto>" e sai   */
}

/*
 * Callback de frame do modo câmera única: repassa o frame para process_image(),
 * a função que cada programa (icap, diagcam...) define para tratar a imagem.
 * É o que liga o núcleo (que não conhece a aplicação) ao programa principal.
 */
static void on_frame_single(void *user, const void *data, size_t bytesused,
                            const struct v4l2_buffer *binf) {

    (void)user;     /* este modo não usa contexto: o estado é global */
    (void)binf;     /* process_image só precisa dos dados e do tamanho */

    process_image(data, (int)bytesused);
}

/*
 * Callback de parada do laço: consulta o teclado entre uma espera e outra.
 * Devolve 0 (parar) quando o usuário digitou 'q'.
 */
static int keep_running_kb(void *user) {

    (void)user;

    while (_kbhit()) {                  /* consome tudo que foi digitado */
        kb_last_key = getch();
    }

    return kb_last_key != 'q';
}

/*
 * Wrapper de compatibilidade para ioctl: delega ao núcleo, que repete a chamada
 * quando ela é interrompida por um sinal (EINTR).
 */
int xioctl(int fh, int request, void *arg) {

    /* O duplo cast evita estender o sinal: os códigos VIDIOC_* têm 32 bits. */
    return v4l2c_xioctl(fh, (unsigned long)(unsigned int)request, arg);

}

/*
 * Abre o descritor do dispositivo V4L2 em modo não-bloqueante.
 * Se dv_n for NULL usa o nome escolhido em choose_v4l2_device() (dev_name).
 * Neste modo (câmera única) qualquer falha encerra o programa, como antes.
 */
int open_v4l2_device(char *dv_n) {

    int fid_l;
    int e;

    if(!dv_n) {
        if(dev_name[0]){
            dv_n = dev_name;                /* usa o dispositivo já escolhido pelo usuário */
        }else{
            fprintf(stderr, "There is not a device name\n");
            exit(EXIT_FAILURE);
        }
    }

    fid_l = v4l2c_open(dv_n);               /* a verificação stat/S_ISCHR agora é feita no núcleo */

    if (-1 == fid_l) {

        e = errno;                          /* guarda: o diagnóstico abaixo usa printf */

        if (ENODEV == e) {                  /* o núcleo devolve ENODEV quando não é dispositivo de caractere */
            fprintf(stderr, "%s is no device\n", dv_n);
        } else {
            errno = e;
            explain_v4l2_error("Abertura do arquivo de dispositivo (open)");
            fprintf(stderr, "Cannot open '%s': %d, %s\n", dv_n, e, strerror(e));
        }
        exit(EXIT_FAILURE);
    }

    return fid_l;

}

/* Fecha o descritor de arquivo associado ao dispositivo de vídeo ativo */
void close_v4l2_device(int fid_l) {

    v4l2ctl_clear(&ctls);                   /* a tabela de controles referia-se a este descritor */

    if (-1 == v4l2c_close(fid_l)) {
        errno_exit("close");
    }

}

/// ***********************************************************************************
/// ****************** CONTROLES DA CÂMERA (EM PERCENTUAL) ******************************
/// ***********************************************************************************

/*
 * Lê do driver todos os controles da câmera e os guarda na tabela interna.
 * Chame depois de open_v4l2_device(). Devolve quantos controles foram achados (< 0 = erro).
 */
int load_controls(int fid_l) {

    return v4l2ctl_populate(&ctls, fid_l);

}

/* Entrega a tabela de controles da câmera única (para listar com list_controls_pct() ou usar a API completa). */
v4l2ctl_set* controls_table(void) {

    return &ctls;

}

/* Procura o controle pelo nome; devolve o índice ou V4L2CTL_ERR_NOFIND. */
static int ctl_index(const char *name) {

    int idx = v4l2ctl_find_name(&ctls, name);
    return (idx < 0) ? V4L2CTL_ERR_NOFIND : idx;

}

/* Ajusta o controle "name" (ex.: "brightness") para pct por cento da faixa min..max. */
int set_control_pct(const char *name, double pct) {

    int idx = ctl_index(name);
    return (idx < 0) ? idx : v4l2ctl_set_pct(&ctls, idx, pct);

}

/* Lê o valor atual do controle "name" como percentual da faixa. */
int get_control_pct(const char *name, double *pct) {

    int idx = ctl_index(name);
    return (idx < 0) ? idx : v4l2ctl_get_pct(&ctls, idx, pct);

}

/* Soma delta_pct ao valor atual do controle "name"; garante andar pelo menos um passo. */
int adjust_control_pct(const char *name, double delta_pct, double *new_pct) {

    int idx = ctl_index(name);
    return (idx < 0) ? idx : v4l2ctl_adjust_pct(&ctls, idx, delta_pct, new_pct);

}

/* Devolve o controle "name" ao valor de fábrica. */
int reset_control(const char *name) {

    int idx = ctl_index(name);
    return (idx < 0) ? idx : v4l2ctl_reset_default(&ctls, idx);

}

/// ***********************************************************************************
/// ************ GET AND SET IMAGE FORMATS, FRAME SIZE AND FRAME INTERVALS ************
/// ***********************************************************************************

/*
 * Configura pixel format, resolução e intervalo entre frames interativamente
 * (menus no terminal). Ao final, w/h/pxfmt guardam o que o DRIVER aceitou,
 * que pode diferir do pedido (o driver ajusta para o modo suportado mais próximo).
 */
int setting_video_format(int fid_l){

    struct v4l2_frmivalenum fl;
    v4l2_session t;                 /* sessão temporária: só para usar as funções de formato do núcleo */

    if (fid_l < 0){
        exit(EXIT_FAILURE);
    }

    CLEAR(fl);                      /* antes não era zerada: 'fl.discrete' podia ter lixo */

    /// *********************************    choose video formats

    printf("\n\n Available image formats for %s:\n", dev_name);

    choose_or_enumerate_image_formats(fid_l, &fl, 0);

    choose_or_enumerate_frame_size(fid_l, &fl, 0);

    choose_or_enumerate_frame_intervals(fid_l, &fl, 0);

    /// ************************* SET NEW FORMAT

    v4l2c_session_init(&t, fid_l);

    /* Uma única chamada S_FMT define formato + resolução (antes eram duas). */
    if (v4l2c_set_format(&t, fl.pixel_format, fl.width, fl.height) < 0) {
        die_session(&t);
    }

    if (v4l2c_set_interval(&t, fl.discrete.numerator, fl.discrete.denominator) < 0) {
        die_session(&t);
    }

    /* O núcleo releu o formato após o S_FMT: estes são os valores REAIS do driver. */
    w     = t.w;
    h     = t.h;
    pxfmt = t.pxfmt;

    return 0;

}

/* Altera apenas o pixel format (mantém a resolução atual). Encerra o programa em caso de erro. */
int set_image_format(int fid_l, __u32 format) {

    v4l2_session t;

    v4l2c_session_init(&t, fid_l);

    if (v4l2c_set_format(&t, format, 0, 0) < 0) {   /* 0, 0 = manter a resolução */
        die_session(&t);
    }

    return fid_l;

}

/* Altera apenas a resolução (mantém o pixel format atual). Encerra o programa em caso de erro. */
int set_image_size(int fid_l, int iw, int ih) {

    v4l2_session t;

    v4l2c_session_init(&t, fid_l);

    if (v4l2c_set_format(&t, 0, iw, ih) < 0) {      /* 0 = manter o pixel format */
        die_session(&t);
    }

    return fid_l;
}

/* Ajusta o intervalo entre frames n/d (n == 0 usa 1/5). Encerra o programa em caso de erro. */
int set_image_intervals(int fid_l, __u32 n, __u32 d) {

    v4l2_session t;

    v4l2c_session_init(&t, fid_l);

    if (v4l2c_set_interval(&t, n, d) < 0) {
        die_session(&t);
    }

    return fid_l;

}

/*
 * Prepara a captura da câmera única: valida o dispositivo, escolhe o método de
 * I/O, aplica/lê o formato e aloca os buffers. O motor de captura em si vive no
 * núcleo (v4l2core); aqui ficam só a política (mensagens e exit) e a ligação
 * com as globais io/w/h/pxfmt usadas por icap.c e pelas conversões de imagem.
 */
void init_device(int fid_l){

    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    int r;

    v4l2c_session_init(&sess, fid_l);       /* zera o estado da sessão desta câmera */

    /// ******************** valida que é um dispositivo de captura de vídeo

    CLEAR(cap);

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

    /// ******************** escolhe/valida o método de I/O

    if((int)io < IO_METHOD_READ || (int)io > IO_METHOD_DMABUF){

        /* io = -1 (padrão): o núcleo descobre qual método está livre (MMAP > USERPTR > READ). */
        r = v4l2c_probe_io(fid_l);

        if (r < 0) {
            fprintf(stderr, (V4L2C_ERR_BUSY == r) ? "%s is busy (in use by another process)\n"
                                                  : "%s has no usable i/o method\n", dev_name);
            exit(EXIT_FAILURE);
        }

        io = (enum io_method)r;

    }else{

        /* O usuário/programa pediu um método específico: confere se o driver o suporta. */
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

    /// ******************** reseta o recorte (crop) para o padrão do dispositivo

    CLEAR(cropcap);
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fid_l, VIDIOC_CROPCAP, &cropcap)) {

        CLEAR(crop);
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect;           /* volta ao retângulo padrão */

        /* Erro ignorado de propósito: muitos dispositivos não suportam crop (EINVAL).
         * O código original imprimia "error found" aqui; foi removido por ser só ruído. */
        xioctl(fid_l, VIDIOC_S_CROP, &crop);
    }

    /// ******************** formato de imagem

    sess.io = (int)io;                      /* o núcleo trabalha com a cópia da sessão */

    if (force_format) {

        /* Força 640x480 YUYV. O núcleo relê o formato depois: o driver pode ter ajustado. */
        r = v4l2c_set_format(&sess, V4L2_PIX_FMT_YUYV, 640, 480);

    } else {

        /* Preserva a configuração atual (ex.: feita antes com v4l2-ctl). */
        r = v4l2c_get_format(&sess);

    }

    if (r < 0) {
        die_session(&sess);
    }

    /* Publica nas globais o formato REAL (usadas por icap.c e pelas conversões de imagem). */
    w     = sess.w;
    h     = sess.h;
    pxfmt = sess.pxfmt;

    /// ******************** buffers

    sess.on_frame = on_frame_single;        /* quem recebe os frames: process_image() via trampolim */
    sess.user     = NULL;

    r = v4l2c_init_buffers(&sess);          /* aloca READ, MMAP ou USERPTR conforme sess.io */

    if (r < 0) {
        die_session(&sess);
    }

}

/* Libera mapeamentos (munmap), buffers do usuário e buffers do driver. */
void uninit_device(int fid_l){

    (void)fid_l;                            /* o descritor já está guardado em sess.fid */

    if (v4l2c_free_buffers(&sess) < 0) {
        die_session(&sess);
    }

}

/* Enfileira os buffers iniciais no driver e liga o streaming. */
void start_capturing(int fid_l) {

    (void)fid_l;

    if (v4l2c_start(&sess) < 0) {
        die_session(&sess);                 /* inclui o diagnóstico de falta de banda USB (ENOSPC) */
    }

}

/* Desliga o streaming. */
void stop_capturing(int fid_l) {

    (void)fid_l;

    if (v4l2c_stop(&sess) < 0) {
        die_session(&sess);
    }

}

/*
 * Laço principal da câmera única: captura e entrega cada frame a process_image()
 * até o usuário digitar 'q'. A espera (select), a leitura e o tratamento de
 * EAGAIN/EINTR estão no núcleo (v4l2c_run); aqui só liga o teclado.
 */
void mainloop(int fid_l){

  int r;

  (void)fid_l;

  kb_last_key = '.';                        /* nenhuma tecla ainda */

  SCRCLR();

  r = v4l2c_run(&sess, keep_running_kb);    /* só retorna ao pressionar 'q' ou em erro */

  if (V4L2C_ERR_TIMEOUT == r) {             /* mantém a mensagem original de timeout */
      fprintf(stderr, "select timeout\n");
      exit(EXIT_FAILURE);
  }

  if (r < 0) {
      die_session(&sess);
  }

}


/*
 * Descobre qual método de I/O está livre no dispositivo (wrapper de compatibilidade
 * sobre v4l2c_probe_io, que contém a lógica corrigida).
 *
 * Mantém o contrato antigo, usado por camview.cpp:
 *    >= 0 : valor de enum io_method (MMAP > USERPTR > READ)
 *     -1  : dispositivo ocupado (EBUSY) - testado como ~IO_METHOD_READ no camview
 *     -2  : erro / dispositivo sem captura
 * Diferente do original NÃO encerra o programa quando o dispositivo não é de
 * captura: devolve -2 e quem chamou decide (no multiview o programa continua).
 */
enum io_method check_io_method_busy(int fid_l){

    int r = v4l2c_probe_io(fid_l);

    if (r >= 0) {
        return (enum io_method)r;               /* método livre encontrado */
    }

    if (V4L2C_ERR_BUSY == r) {
        return (enum io_method)-1;              /* ocupado */
    }

    return (enum io_method)-2;                  /* qualquer outro erro */

}

/*
 * Converte o frame bruto da câmera para tons de cinza (1 byte por pixel), usando o
 * pixel format e as dimensões atuais (globais pxfmt, w e h). Para MJPEG, w e h passam
 * a valer o tamanho real da imagem decodificada.
 * Devolve um buffer alocado (quem chamou faz free) ou NULL, com aviso, se o formato
 * não tiver conversão. O trabalho em si é feito pelo módulo v4l2gray.
 */
unsigned char* convert_to_grayscale(const unsigned char* raw_buffer, unsigned long buffer_size) {

    if (!v4l2g_is_supported(pxfmt)) {

        char fourcc[5];                         /* o FOURCC tem 4 letras; precisa de um '\0' para virar string */

        memcpy(fourcc, &pxfmt, 4);
        fourcc[4] = '\0';

        fprintf(stderr, "Formato FourCC '%s' não suportado em modo texto.\n", fourcc);
        return NULL;
    }

    return v4l2g_to_gray(pxfmt, raw_buffer, buffer_size, &h, &w);
}

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

