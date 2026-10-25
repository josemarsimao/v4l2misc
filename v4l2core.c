/*
 *  V4L2 video capture: My tests
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
 *  v4l2core.c - NÚCLEO DE CAPTURA V4L2 (implementação)
 *
 *  Veja v4l2core.h para o contrato da camada (sem exit, sem printf, sem globais,
 *  frames entregues por callback). Este arquivo unifica o que antes existia duas
 *  vezes: init_mmap/userp/read, start/stop_capturing, read_frame, mainloop e
 *  uninit_device (v4l2misc.c) e os cam_* equivalentes (camview.cpp).
 *  ---------------------------------------------------------------------------
 */

#include "v4l2core.h"

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>

/* Quantidade de buffers pedidos ao driver (MMAP e USERPTR). */
#define V4L2C_NUM_BUFFERS 4

/* Tempo padrão (segundos) sem frames antes de declarar timeout. */
#define V4L2C_DEFAULT_TIMEOUT 5

/* ========================================================================== */
/*  Funções internas                                                          */
/* ========================================================================== */

/*
 * Registra o erro na sessão (errno + texto do contexto) e devolve o código.
 * Deve ser chamada logo após a chamada de sistema que falhou, para que o
 * errno ainda seja o dela.
 */
static int fail(v4l2_session *s, const char *ctx, int code) {

    if (s) {
        s->err_no  = errno;     /* guarda o errno AGORA: chamadas seguintes podem sobrescrevê-lo */
        s->err_ctx = ctx;       /* literal de string: não precisa de cópia nem de free          */
    }
    return code;
}

/*
 * Converte o nosso enum io_method para o tipo de memória do V4L2.
 * ATENÇÃO: os valores NÃO coincidem (IO_METHOD_MMAP = 2, V4L2_MEMORY_MMAP = 1).
 * Usar o enum direto em req.memory (como fazia free_v4l2_video_buffers) pede o
 * tipo errado de memória; por isso a conversão é sempre feita aqui.
 */
static __u32 memory_type(int io) {

    switch (io) {
        case IO_METHOD_MMAP:    return V4L2_MEMORY_MMAP;
        case IO_METHOD_USERPTR: return V4L2_MEMORY_USERPTR;
        case IO_METHOD_DMABUF:  return V4L2_MEMORY_DMABUF;
        default:                return 0;
    }
}

/*
 * Pede ao driver para liberar (REQBUFS com count = 0) os buffers de um tipo de
 * memória. É o par de v4l2c_init_buffers do lado do kernel. EBUSY é ignorado:
 * significa que ainda há mapeamento/streaming e o driver liberará depois.
 */
static int release_driver_buffers(v4l2_session *s, __u32 memory) {

    struct v4l2_requestbuffers req;

    CLEAR(req);
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = memory;
    req.count  = 0;                         /* count = 0 significa "libere tudo" */

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_REQBUFS, &req) && errno != EBUSY) {
        return fail(s, "VIDIOC_REQBUFS (liberar buffers)", V4L2C_ERR_IOCTL);
    }
    return V4L2C_OK;
}

/*
 * Testa se o driver aceita um tipo de memória, SEM deixar buffers alocados.
 * Retorna V4L2C_OK, V4L2C_ERR_BUSY (outro processo usando) ou V4L2C_ERR_UNSUPPORTED.
 */
static int try_reqbufs(int fid, __u32 memory) {

    struct v4l2_requestbuffers req;

    CLEAR(req);
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = memory;
    req.count  = 1;

    if (-1 == v4l2c_xioctl(fid, VIDIOC_REQBUFS, &req)) {
        return (EBUSY == errno) ? V4L2C_ERR_BUSY : V4L2C_ERR_UNSUPPORTED;
    }

    req.count = 0;                          /* desfaz o teste: devolve o buffer ao driver */
    v4l2c_xioctl(fid, VIDIOC_REQBUFS, &req);

    return V4L2C_OK;
}

/* Entrega o frame do buffer 'idx' ao callback da aplicação (se houver). */
static void deliver(v4l2_session *s, unsigned int idx, size_t used) {

    if (s->on_frame) {
        s->on_frame(s->user, s->buffers[idx].start, used, &s->buffers[idx].binf);
    }
}

/* ========================================================================== */
/*  Utilitários sem sessão                                                    */
/* ========================================================================== */

/*
 * ioctl que repete a chamada quando é interrompida por um sinal (EINTR).
 * Substitui xioctl() de v4l2misc.c (que passa a ser um wrapper deste).
 */
int v4l2c_xioctl(int fh, unsigned long request, void *arg) {

    int r;

    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);    /* só repete se foi sinal; outro erro sai do laço */

    return r;
}

/*
 * Abre o nó de dispositivo (ex.: /dev/video0) em modo não-bloqueante.
 * Diferente de open_v4l2_device(), NÃO encerra o programa: devolve -1 e deixa
 * o errno para a aplicação decidir. Isso permite varrer câmeras com hot-plug
 * (make_camera_list) sem que uma câmera que sumiu derrube o processo.
 */
int v4l2c_open(const char *path) {

    struct stat st;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if (-1 == stat(path, &st)) {            /* o arquivo existe? */
        return -1;
    }

    if (!S_ISCHR(st.st_mode)) {             /* dispositivos V4L2 são de caractere */
        errno = ENODEV;
        return -1;
    }

    return open(path, O_RDWR | O_NONBLOCK, 0);
}

/* Fecha o descritor do dispositivo. Retorna 0 em sucesso, -1 em erro (errno). */
int v4l2c_close(int fid) {

    return close(fid);
}

/*
 * Descobre qual método de I/O está livre e funciona neste dispositivo.
 * Ordem de preferência (a mesma do código original): MMAP > USERPTR > READ.
 *
 * Retorno: um valor de enum io_method (>= 0) ou
 *          V4L2C_ERR_BUSY        - dispositivo já em uso por outro processo,
 *          V4L2C_ERR_UNSUPPORTED - não captura vídeo ou nenhum método funciona,
 *          V4L2C_ERR_IOCTL       - VIDIOC_QUERYCAP falhou.
 *
 * Corrige check_io_method_busy(): lá a variável de retorno podia ficar sem
 * inicializar e o resultado do USERPTR era sempre sobrescrito pelo do MMAP.
 */
int v4l2c_probe_io(int fid) {

    struct v4l2_capability cap;
    __u32 caps;
    int r;

    CLEAR(cap);
    if (-1 == v4l2c_xioctl(fid, VIDIOC_QUERYCAP, &cap)) {
        return V4L2C_ERR_IOCTL;
    }

    /* device_caps descreve ESTE nó; 'capabilities' descreve o dispositivo inteiro.
     * Em câmeras UVC o nó de metadados (/dev/video1) herda VIDEO_CAPTURE do
     * dispositivo mas não captura imagem; device_caps evita confundi-lo com câmera. */
    caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;

    if (!(caps & V4L2_CAP_VIDEO_CAPTURE)) {
        return V4L2C_ERR_UNSUPPORTED;
    }

    if (caps & V4L2_CAP_STREAMING) {

        r = try_reqbufs(fid, V4L2_MEMORY_MMAP);
        if (V4L2C_OK == r)        return IO_METHOD_MMAP;
        if (V4L2C_ERR_BUSY == r)  return V4L2C_ERR_BUSY;    /* ocupado: não adianta tentar outro */

        r = try_reqbufs(fid, V4L2_MEMORY_USERPTR);
        if (V4L2C_OK == r)        return IO_METHOD_USERPTR;
        if (V4L2C_ERR_BUSY == r)  return V4L2C_ERR_BUSY;
    }

    if (caps & V4L2_CAP_READWRITE) {
        return IO_METHOD_READ;
    }

    return V4L2C_ERR_UNSUPPORTED;
}

/* ========================================================================== */
/*  Sessão: formato e intervalo                                               */
/* ========================================================================== */

/*
 * Prepara uma sessão nova: zera tudo e guarda o descritor.
 * io = -1 pede detecção automática (feita em v4l2c_capture).
 */
void v4l2c_session_init(v4l2_session *s, int fid) {

    CLEAR(*s);
    s->fid         = fid;
    s->io          = -1;
    s->timeout_sec = V4L2C_DEFAULT_TIMEOUT;
}

/*
 * Lê do driver o formato atual (largura, altura, pixel format) e calcula o
 * tamanho máximo de um frame (buffer_maxsize).
 * Unifica update_cam_format() + cam_def_buffer_maxsize() do camview.cpp.
 */
int v4l2c_get_format(v4l2_session *s) {

    struct v4l2_format fmt;
    unsigned int min;

    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_G_FMT, &fmt)) {
        return fail(s, "VIDIOC_G_FMT (ler formato atual)", V4L2C_ERR_IOCTL);
    }

    s->w     = fmt.fmt.pix.width;
    s->h     = fmt.fmt.pix.height;
    s->pxfmt = fmt.fmt.pix.pixelformat;
    s->buffer_maxsize = fmt.fmt.pix.sizeimage;

    /* "Buggy driver paranoia": alguns drivers deixam sizeimage menor que o
     * necessário (ou zero). Garante pelo menos bytesperline * altura e, se o
     * driver não informou nem bytesperline, assume 2 bytes por pixel (YUYV). */
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (0 == min) {
        min = fmt.fmt.pix.width * 2 * fmt.fmt.pix.height;
    }
    if (s->buffer_maxsize < min) {
        s->buffer_maxsize = min;
    }

    return V4L2C_OK;
}

/*
 * Define pixel format e resolução numa única chamada VIDIOC_S_FMT.
 * Passe 0 em pixfmt, w ou h para MANTER o valor atual daquele campo.
 * Depois relê o formato, porque o driver pode ajustar os valores pedidos
 * (ele escolhe a resolução suportada mais próxima).
 */
int v4l2c_set_format(v4l2_session *s, __u32 pixfmt, int w, int h) {

    struct v4l2_format fmt;

    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    /* Parte do formato atual para preservar os campos que não vamos mexer. */
    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_G_FMT, &fmt)) {
        return fail(s, "VIDIOC_G_FMT (antes de alterar o formato)", V4L2C_ERR_IOCTL);
    }

    if (pixfmt)           fmt.fmt.pix.pixelformat = pixfmt;
    if (w > 0 && h > 0) { fmt.fmt.pix.width = w; fmt.fmt.pix.height = h; }

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_S_FMT, &fmt)) {
        return fail(s, "VIDIOC_S_FMT (definir formato e resolução)", V4L2C_ERR_IOCTL);
    }

    return v4l2c_get_format(s);     /* descobre o que o driver realmente aceitou */
}

/*
 * Define o intervalo entre frames (n/d segundos; 1/30 = 30 fps).
 * Se n == 0 usa 1/5 (comportamento herdado de set_image_intervals()).
 */
int v4l2c_set_interval(v4l2_session *s, __u32 n, __u32 d) {

    struct v4l2_streamparm sp;

    CLEAR(sp);
    sp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_G_PARM, &sp)) {
        return fail(s, "VIDIOC_G_PARM (ler intervalo)", V4L2C_ERR_IOCTL);
    }

    if (0 == n) {               /* valor inválido: cai no padrão 1/5 */
        n = 1;
        d = 5;
    }

    sp.parm.capture.timeperframe.numerator   = n;
    sp.parm.capture.timeperframe.denominator = d;

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_S_PARM, &sp)) {
        return fail(s, "VIDIOC_S_PARM (definir intervalo)", V4L2C_ERR_IOCTL);
    }

    return V4L2C_OK;
}

/* ========================================================================== */
/*  Sessão: buffers                                                           */
/* ========================================================================== */

/* READ: um único buffer comum; os dados chegam com read(). */
static int init_read(v4l2_session *s) {

    s->buffers = (vbuff *)calloc(1, sizeof(vbuff));
    if (!s->buffers) {
        errno = ENOMEM;
        return fail(s, "calloc (vetor de buffers READ)", V4L2C_ERR_NOMEM);
    }

    s->buffers[0].length = s->buffer_maxsize;
    s->buffers[0].start  = malloc(s->buffer_maxsize);

    if (!s->buffers[0].start) {
        free(s->buffers);
        s->buffers = NULL;
        errno = ENOMEM;
        return fail(s, "malloc (buffer READ)", V4L2C_ERR_NOMEM);
    }

    s->num_buf = 1;
    return V4L2C_OK;
}

/*
 * MMAP: o driver aloca os buffers no kernel e nós os mapeamos no espaço do
 * usuário (zero-cópia). Se qualquer passo falhar, desfaz o que já foi mapeado.
 */
static int init_mmap(v4l2_session *s) {

    struct v4l2_requestbuffers req;
    unsigned int i;
    int rc;

    CLEAR(req);
    req.count  = V4L2C_NUM_BUFFERS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_REQBUFS, &req)) {
        return fail(s, "VIDIOC_REQBUFS (MMAP)",
                    (EINVAL == errno) ? V4L2C_ERR_UNSUPPORTED : V4L2C_ERR_IOCTL);
    }

    /* O driver pode conceder menos buffers que o pedido; com menos de 2 não há
     * como capturar enquanto a aplicação processa um frame. */
    if (req.count < 2) {
        release_driver_buffers(s, V4L2_MEMORY_MMAP);
        errno = ENOMEM;
        return fail(s, "buffers insuficientes no driver (MMAP)", V4L2C_ERR_NOMEM);
    }

    s->buffers = (vbuff *)calloc(req.count, sizeof(vbuff));
    if (!s->buffers) {
        release_driver_buffers(s, V4L2_MEMORY_MMAP);
        errno = ENOMEM;
        return fail(s, "calloc (vetor de buffers MMAP)", V4L2C_ERR_NOMEM);
    }

    s->num_buf = 0;             /* conta apenas os buffers JÁ mapeados (usado no desfazer) */

    for (i = 0; i < req.count; i++) {

        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        /* QUERYBUF informa o tamanho e o "offset" do buffer dentro do driver. */
        if (-1 == v4l2c_xioctl(s->fid, VIDIOC_QUERYBUF, &buf)) {
            rc = fail(s, "VIDIOC_QUERYBUF (MMAP)", V4L2C_ERR_IOCTL);
            v4l2c_free_buffers(s);
            return rc;
        }

        s->buffers[i].length = buf.length;
        s->buffers[i].start  = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                    MAP_SHARED, s->fid, buf.m.offset);

        if (MAP_FAILED == s->buffers[i].start) {
            s->buffers[i].start = NULL;     /* marca como não mapeado para o desfazer */
            rc = fail(s, "mmap()", V4L2C_ERR_NOMEM);
            v4l2c_free_buffers(s);
            return rc;
        }

        s->num_buf = i + 1;

        if (buf.length > s->buffer_maxsize) {
            s->buffer_maxsize = buf.length;
        }
    }

    return V4L2C_OK;
}

/*
 * USERPTR: a aplicação aloca a memória (malloc) e a empresta ao driver.
 */
static int init_userp(v4l2_session *s) {

    struct v4l2_requestbuffers req;
    unsigned int i;
    int rc;

    CLEAR(req);
    req.count  = V4L2C_NUM_BUFFERS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_REQBUFS, &req)) {
        return fail(s, "VIDIOC_REQBUFS (USERPTR)",
                    (EINVAL == errno) ? V4L2C_ERR_UNSUPPORTED : V4L2C_ERR_IOCTL);
    }

    s->buffers = (vbuff *)calloc(V4L2C_NUM_BUFFERS, sizeof(vbuff));
    if (!s->buffers) {
        release_driver_buffers(s, V4L2_MEMORY_USERPTR);
        errno = ENOMEM;
        return fail(s, "calloc (vetor de buffers USERPTR)", V4L2C_ERR_NOMEM);
    }

    s->num_buf = 0;

    for (i = 0; i < V4L2C_NUM_BUFFERS; i++) {

        s->buffers[i].length = s->buffer_maxsize;
        s->buffers[i].start  = malloc(s->buffer_maxsize);

        if (!s->buffers[i].start) {
            errno = ENOMEM;
            rc = fail(s, "malloc (buffer USERPTR)", V4L2C_ERR_NOMEM);
            v4l2c_free_buffers(s);          /* libera só os i buffers já alocados */
            return rc;
        }

        s->num_buf = i + 1;
    }

    return V4L2C_OK;
}

/*
 * Aloca os buffers conforme o método de I/O escolhido em s->io.
 * Requer que v4l2c_get_format() já tenha sido chamada (usa buffer_maxsize).
 * DMABUF ainda não é implementado (mesmo estado do código original).
 */
int v4l2c_init_buffers(v4l2_session *s) {

    switch (s->io) {

        case IO_METHOD_READ:    return init_read(s);
        case IO_METHOD_MMAP:    return init_mmap(s);
        case IO_METHOD_USERPTR: return init_userp(s);

        default:
            errno = EINVAL;
            return fail(s, "método de I/O não suportado (DMABUF/indefinido)", V4L2C_ERR_UNSUPPORTED);
    }
}

/*
 * Libera tudo que v4l2c_init_buffers criou: desfaz os mapeamentos, libera a
 * memória do usuário e devolve os buffers ao driver. É idempotente: chamar
 * duas vezes (ou sem ter alocado) é seguro. Também é usada para desfazer
 * alocações parciais quando init_* falha no meio.
 */
int v4l2c_free_buffers(v4l2_session *s) {

    unsigned int i;
    int rc = V4L2C_OK;

    if (!s->buffers) {
        return V4L2C_OK;
    }

    switch (s->io) {

        case IO_METHOD_READ:
            free(s->buffers[0].start);
            break;

        case IO_METHOD_MMAP:
            /* Primeiro desfaz os mapeamentos (REQBUFS 0 falha com EBUSY se houver mmap ativo). */
            for (i = 0; i < s->num_buf; i++) {
                if (s->buffers[i].start && -1 == munmap(s->buffers[i].start, s->buffers[i].length)) {
                    rc = fail(s, "munmap()", V4L2C_ERR_IOCTL);
                }
            }
            release_driver_buffers(s, V4L2_MEMORY_MMAP);
            break;

        case IO_METHOD_USERPTR:
            /* Devolve ao driver ANTES de liberar a memória que ele conhecia. */
            release_driver_buffers(s, V4L2_MEMORY_USERPTR);
            for (i = 0; i < s->num_buf; i++) {
                free(s->buffers[i].start);
            }
            break;

        default:
            break;
    }

    free(s->buffers);
    s->buffers = NULL;
    s->num_buf = 0;

    return rc;
}

/*
 * Pede ao driver para liberar os buffers do tipo de memória indicado em s->io
 * (REQBUFS com count = 0), SEM tocar nos buffers do lado da aplicação.
 * Usada quando só o descritor e o método de I/O são conhecidos (ex.: após mudar
 * o formato da câmera). Para READ ou io indefinido não há nada no driver: retorna OK.
 */
int v4l2c_release_driver(v4l2_session *s) {

    __u32 mem = memory_type(s->io);

    if (0 == mem) {
        return V4L2C_OK;
    }

    return release_driver_buffers(s, mem);
}

/* ========================================================================== */
/*  Sessão: streaming                                                         */
/* ========================================================================== */

/*
 * Coloca todos os buffers na fila do driver (VIDIOC_QBUF) e liga o streaming
 * (VIDIOC_STREAMON). No modo READ não há nada a fazer: o driver só captura
 * quando a aplicação chama read().
 * Em caso de erro NÃO limpa nada: a aplicação (ou v4l2c_capture) chama
 * v4l2c_stop e v4l2c_free_buffers.
 */
int v4l2c_start(v4l2_session *s) {

    unsigned int i;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (IO_METHOD_READ == s->io) {
        return V4L2C_OK;
    }

    if (IO_METHOD_MMAP != s->io && IO_METHOD_USERPTR != s->io) {
        errno = EINVAL;
        return fail(s, "start: método de I/O não suportado", V4L2C_ERR_UNSUPPORTED);
    }

    for (i = 0; i < s->num_buf; i++) {

        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = memory_type(s->io);
        buf.index  = i;

        if (IO_METHOD_USERPTR == s->io) {
            /* No USERPTR o driver precisa conhecer o endereço e o tamanho. */
            buf.m.userptr = (unsigned long)s->buffers[i].start;
            buf.length    = s->buffers[i].length;
        }

        if (-1 == v4l2c_xioctl(s->fid, VIDIOC_QBUF, &buf)) {
            return fail(s, "VIDIOC_QBUF (fila inicial)", V4L2C_ERR_IOCTL);
        }
    }

    /* ENOSPC aqui normalmente é falta de banda USB: explain_v4l2_error() trata esse caso. */
    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_STREAMON, &type)) {
        return fail(s, "VIDIOC_STREAMON (iniciar streaming)", V4L2C_ERR_IOCTL);
    }

    return V4L2C_OK;
}

/*
 * Desliga o streaming (VIDIOC_STREAMOFF). Chamar sem ter iniciado é inofensivo.
 * Se a câmera foi desconectada (ENODEV/EBADF) retorna erro, mas a aplicação
 * deve continuar a limpeza normalmente (v4l2c_free_buffers).
 */
int v4l2c_stop(v4l2_session *s) {

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (IO_METHOD_READ == s->io) {
        return V4L2C_OK;
    }

    if (IO_METHOD_MMAP != s->io && IO_METHOD_USERPTR != s->io) {
        return V4L2C_OK;        /* nada foi ligado */
    }

    if (-1 == v4l2c_xioctl(s->fid, VIDIOC_STREAMOFF, &type)) {
        return fail(s, "VIDIOC_STREAMOFF (parar streaming)", V4L2C_ERR_IOCTL);
    }

    return V4L2C_OK;
}

/*
 * Lê UM frame, entrega ao callback on_frame e devolve o buffer ao driver.
 *
 * Retorno (convenção ÚNICA, no lugar das duas que existiam e divergiam):
 *    V4L2C_GOT_FRAME (1) : frame entregue ao callback
 *    V4L2C_NO_FRAME  (0) : ainda não há frame (EAGAIN) - tente de novo
 *    < 0                 : erro (ver s->err_no / s->err_ctx)
 */
int v4l2c_read_frame(v4l2_session *s) {

    struct v4l2_buffer buf;
    unsigned int i;
    size_t used;

    switch (s->io) {

        case IO_METHOD_READ: {

            /* read() devolve quantos bytes vieram: é o tamanho real do frame. */
            ssize_t n = read(s->fid, s->buffers[0].start, s->buffers[0].length);

            if (-1 == n) {
                if (EAGAIN == errno || EINTR == errno) {
                    return V4L2C_NO_FRAME;
                }
                return fail(s, "read()", V4L2C_ERR_IOCTL);
            }

            s->bon = 0;
            CLEAR(s->buffers[0].binf);
            s->buffers[0].binf.bytesused = (__u32)n;   /* conversores de MJPEG usam bytesused */
            deliver(s, 0, (size_t)n);
            return V4L2C_GOT_FRAME;
        }

        case IO_METHOD_MMAP:

            CLEAR(buf);
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;

            /* DQBUF retira da fila do driver um buffer já preenchido. */
            if (-1 == v4l2c_xioctl(s->fid, VIDIOC_DQBUF, &buf)) {
                if (EAGAIN == errno) {
                    return V4L2C_NO_FRAME;      /* driver ainda não terminou o frame */
                }
                return fail(s, "VIDIOC_DQBUF (MMAP)", V4L2C_ERR_IOCTL);
            }

            if (buf.index >= s->num_buf) {      /* substitui o assert() do código original */
                errno = EINVAL;
                return fail(s, "índice de buffer inválido (MMAP)", V4L2C_ERR_IOCTL);
            }

            s->bon = (int)buf.index;
            s->buffers[buf.index].binf = buf;   /* guarda bytesused e demais metadados */

            used = buf.bytesused;
            if (used > s->buffers[buf.index].length) {
                used = s->buffers[buf.index].length;    /* nunca informa mais do que o buffer comporta */
            }

            deliver(s, buf.index, used);        /* a aplicação usa o frame AQUI, zero-cópia */

            /* Devolve o buffer ao driver para ser preenchido de novo. */
            if (-1 == v4l2c_xioctl(s->fid, VIDIOC_QBUF, &buf)) {
                return fail(s, "VIDIOC_QBUF (devolução MMAP)", V4L2C_ERR_IOCTL);
            }
            return V4L2C_GOT_FRAME;

        case IO_METHOD_USERPTR:

            CLEAR(buf);
            buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_USERPTR;

            if (-1 == v4l2c_xioctl(s->fid, VIDIOC_DQBUF, &buf)) {
                if (EAGAIN == errno) {
                    return V4L2C_NO_FRAME;
                }
                return fail(s, "VIDIOC_DQBUF (USERPTR)", V4L2C_ERR_IOCTL);
            }

            /* No USERPTR o driver devolve o ponteiro, não o índice: descobre qual buffer é. */
            for (i = 0; i < s->num_buf; i++) {
                if (buf.m.userptr == (unsigned long)s->buffers[i].start &&
                    buf.length == s->buffers[i].length) {
                    break;
                }
            }

            if (i >= s->num_buf) {
                errno = EINVAL;
                return fail(s, "ponteiro de buffer desconhecido (USERPTR)", V4L2C_ERR_IOCTL);
            }

            s->bon = (int)i;
            s->buffers[i].binf = buf;

            used = buf.bytesused;
            if (used > s->buffers[i].length) {
                used = s->buffers[i].length;
            }

            deliver(s, i, used);

            if (-1 == v4l2c_xioctl(s->fid, VIDIOC_QBUF, &buf)) {
                return fail(s, "VIDIOC_QBUF (devolução USERPTR)", V4L2C_ERR_IOCTL);
            }
            return V4L2C_GOT_FRAME;

        default:
            errno = EINVAL;
            return fail(s, "read_frame: método de I/O não suportado", V4L2C_ERR_UNSUPPORTED);
    }
}

/*
 * Laço principal: espera a câmera com select() e lê os frames até que
 * keep_running() devolva 0, ocorra um erro ou a câmera pare de enviar frames.
 *
 * Diferença em relação a mainloop()/cam_mainloop(): o select espera no máximo
 * 1 s por vez, então keep_running() é consultado pelo menos uma vez por segundo
 * mesmo com a câmera parada. Antes, o pedido de parada (tecla 'q' ou 'E' no
 * multiview) só era visto quando chegava um frame ou ao fim do timeout de 5 s.
 *
 * Retorno: V4L2C_OK se parou por pedido da aplicação; erro (<0) caso contrário.
 */
int v4l2c_run(v4l2_session *s, v4l2c_run_cb keep_running) {

    int idle = 0;       /* segundos consecutivos sem nenhum frame */
    int limit = (s->timeout_sec > 0) ? s->timeout_sec : V4L2C_DEFAULT_TIMEOUT;

    while (!keep_running || keep_running(s->user)) {

        fd_set fds;
        struct timeval tv;
        int r;

        FD_ZERO(&fds);
        FD_SET(s->fid, &fds);
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        r = select(s->fid + 1, &fds, NULL, NULL, &tv);

        if (-1 == r) {
            if (EINTR == errno) {
                continue;               /* interrompido por sinal: tenta de novo */
            }
            return fail(s, "select()", V4L2C_ERR_IOCTL);
        }

        if (0 == r) {                   /* passou 1 s sem dados */
            if (++idle >= limit) {
                errno = ETIMEDOUT;
                return fail(s, "select(): câmera parou de enviar frames", V4L2C_ERR_TIMEOUT);
            }
            continue;                   /* volta a consultar keep_running() */
        }

        idle = 0;                       /* chegou dado: zera a contagem de ociosidade */

        r = v4l2c_read_frame(s);
        if (r < 0) {
            return r;                   /* erro real: propaga */
        }
        /* r == V4L2C_NO_FRAME (EAGAIN): apenas volta ao select. */
    }

    return V4L2C_OK;
}

/* ========================================================================== */
/*  Atalho do ciclo completo                                                  */
/* ========================================================================== */

/*
 * Executa o ciclo completo de uma câmera: lê o formato, escolhe o método de
 * I/O (se s->io for -1), aloca buffers, liga o streaming, captura até
 * keep_running() pedir parada e, SEMPRE, desliga e libera tudo.
 * Substitui capture_pictures_v4l2() do camview.cpp.
 *
 * O primeiro erro ocorrido é o que fica registrado em s->err_no/err_ctx;
 * falhas na limpeza não o sobrescrevem.
 */
int v4l2c_capture(v4l2_session *s, v4l2c_run_cb keep_running) {

    int r;
    int io;
    int saved_no;
    const char *saved_ctx;

    r = v4l2c_get_format(s);
    if (r < 0) {
        return r;
    }

    /* io fora de [READ..DMABUF] significa "automático": pergunta ao driver. */
    if (s->io < IO_METHOD_READ || s->io > IO_METHOD_DMABUF) {
        io = v4l2c_probe_io(s->fid);
        if (io < 0) {
            errno = (V4L2C_ERR_BUSY == io) ? EBUSY : ENOTSUP;
            return fail(s, "nenhum método de I/O disponível", io);
        }
        s->io = io;
    }

    r = v4l2c_init_buffers(s);
    if (r < 0) {
        return r;                       /* init_* já desfez o que alocou */
    }

    r = v4l2c_start(s);
    if (V4L2C_OK == r) {
        r = v4l2c_run(s, keep_running);
    }

    /* Limpeza obrigatória. Guarda o erro original porque stop/free podem
     * falhar (ex.: câmera desconectada) e sobrescreveriam err_no/err_ctx. */
    saved_no  = s->err_no;
    saved_ctx = s->err_ctx;

    {
        int rs = v4l2c_stop(s);
        int rf = v4l2c_free_buffers(s);

        if (r >= 0) {                   /* captura terminou bem: reporta falha da limpeza, se houver */
            if (rs < 0)      r = rs;
            else if (rf < 0) r = rf;
        } else {                        /* já havia erro: preserva o primeiro */
            s->err_no  = saved_no;
            s->err_ctx = saved_ctx;
        }
    }

    return r;
}
