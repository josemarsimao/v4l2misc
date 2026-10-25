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
 *  v4l2enum.c - DESCOBERTA, ENUMERAÇÃO E DIAGNÓSTICO DE DISPOSITIVOS V4L2
 *
 *  Veja v4l2enum.h. Este módulo só CONSULTA o driver e mostra/pede informação no
 *  terminal; não captura frames. Depende apenas de v4l2core.
 *
 *  Origem: as funções vieram de v4l2misc.c (etapa 4). As marcadas como
 *  "revisada" receberam correção; as demais foram movidas sem alteração de lógica.
 *  ---------------------------------------------------------------------------
 */

#include "v4l2enum.h"

#include <ctype.h>
#include "v4l2term.h"     /// só a macro SCRCLR (usada em show_video_list); não exige v4l2term.c

/*
 * O código de enumeração usa o nome curto xioctl(). Aqui ele aponta para o ioctl
 * do núcleo (que repete a chamada em caso de EINTR), sem depender de v4l2misc.c.
 */
#define xioctl(fh, request, arg) v4l2c_xioctl((fh), (unsigned long)(request), (arg))

/* ========================================================================== */
/*  Dados do módulo                                                           */
/* ========================================================================== */

char dev_name[256];                                    /* caminho do dispositivo escolhido (/dev/videoN)    */
static v4l2_id v4l2_device_list[MAX_V4L2_DEV_NUM];     /* dispositivos achados na última varredura de /dev  */
static int     num_dev_found = 0;                      /* quantos itens válidos em v4l2_device_list         */


/// https://linuxtv.org/downloads/v4l-dvb-apis/kapi/v4l2-dev.html#video-device-registration
///VFL_TYPE_GRABBER    /dev/videoX         for video input/output devices
///VFL_TYPE_VBI        /dev/vbiX           for vertical blank data (i.e. closed captions, teletext)
///VFL_TYPE_RADIO      /dev/radioX         for radio tuners
///VFL_TYPE_SUBDEV     /dev/v4l-subdevX    for V4L2 subdevices
///VFL_TYPE_SDR        /dev/swradioX       for Software Defined Radio (SDR) tuners
///VFL_TYPE_TOUCH      /dev/v4l-touchX     for touch sensors

/* Prefixos dos nós em /dev, na MESMA ordem de enum v4l2_device_type. */
const char *prefixes[] = {
    "video",
    "vbi",
    "radio",
    "v4l-subdev",
    "swradio",
    "v4l-touch",
    NULL
};

/* ========================================================================== */
/*  1. Diagnóstico de erros                                                   */
/* ========================================================================== */

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

/* ========================================================================== */
/*  2. Descoberta de dispositivos                                             */
/* ========================================================================== */

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

/*
 * Avalia se um nome de arquivo em /dev casa com os prefixos do subsistema V4L2
 * (revisada: usa strncmp, que não lê além do fim do nome, e isdigit com unsigned char).
 * Se casar, devolve o tipo em *typ e o número do nó em *num (ex.: "video2" -> VIDEO, 2).
 */
static bool is_v4l2_dev(const char *name, enum v4l2_device_type* typ, int* num) {

    size_t l;

    for(*typ = V4L2_DEV_VIDEO; *typ <= V4L2_DEV_V4L_TOUCH; (*typ)++) {

        l = strlen(prefixes[*typ]);                         /* tamanho do prefixo desse tipo */

        if (!strncmp(name, prefixes[*typ], l)) {            /* o nome começa com o prefixo? */
            if (isdigit((unsigned char)name[l])){           /* e logo depois vem um número? */
                sscanf(&name[l], "%d", num);
                return true;
            }
        }

    }

    return false;

}

/*
 * Preenche 'list' com os dispositivos V4L2 encontrados em /dev, filtrando por tipo (dvtp).
 * Só entram os nós que abrem e respondem a VIDIOC_QUERYCAP.
 * Retorna quantos foram achados, ou -1 se /dev não pôde ser lido.
 *
 * Revisada: antes, falha ao abrir /dev encerrava o programa (exit). Como o multiview
 * chama esta varredura a cada segundo, agora ela só avisa e devolve -1, e a lista
 * anterior é mantida intacta (o memset só acontece depois de abrir /dev).
 */
static int get_v4l2_device_list(v4l2_id *list, enum v4l2_device_type dvtp, int sz) {

    int idx = 0;
    DIR *dp;
    char nameofdevice[300];

    struct dirent *ep;
    struct v4l2_capability vcap;
    int fd = -1;
    int err;

    int num;                        /// device node number
    enum v4l2_device_type typ;      /// device node type

    dp = opendir("/dev");

    if (dp == NULL) {

        perror ("Couldn't open the directory /dev/");
        return -1;

    }

    memset(list,-1,sz*sizeof(v4l2_id));     /* limpa a lista só agora que /dev abriu */

    while (idx < sz && (ep = readdir(dp))) {

        if (is_v4l2_dev(ep->d_name, &typ, &num)) {

            if(dvtp == V4L2_DEV_ALL || dvtp == typ){

                snprintf(nameofdevice, sizeof(nameofdevice), "/dev/%s", ep->d_name);

                fd = open(nameofdevice, O_RDWR  | O_NONBLOCK, 0);
                if (fd < 0) continue;                   /* sem permissão ou sumiu: ignora */

                CLEAR(vcap);
                err = xioctl(fd, VIDIOC_QUERYCAP, &vcap); /// sem erro = cumpre a especificação V4L2
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

/*
 * Varre /dev e guarda TODOS os tipos de dispositivo V4L2 na lista interna.
 * Se a varredura falhar, mantém a lista anterior. Retorna quantos há na lista.
 */
int make_v4l2_list() {

    int n = get_v4l2_device_list(v4l2_device_list, V4L2_DEV_ALL, MAX_V4L2_DEV_NUM);

    if (n >= 0) {
        num_dev_found = n;
    }

    return num_dev_found;

}

/*
 * Varre /dev e guarda só os dispositivos de vídeo (/dev/video*) na lista interna.
 * Se a varredura falhar, mantém a lista anterior. Retorna quantos há na lista.
 */
int make_video_list() {

    int n = get_v4l2_device_list(v4l2_device_list, V4L2_DEV_VIDEO, MAX_V4L2_DEV_NUM);

    if (n >= 0) {
        num_dev_found = n;
    }

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

/* ========================================================================== */
/*  3. Listagem e diagnóstico (só leitura do driver)                          */
/* ========================================================================== */

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

/* Lista os padrões de vídeo suportados pelo hardware (ex: PAL, NTSC) */
int enumerate_video_standards(int fid_l) {

    int idx = 0;
    struct v4l2_standard        stdvid;
    printf("Enumerate supported video standards.\n");
    memset(&stdvid, 0, sizeof(stdvid));
    stdvid.index = idx;
    while(xioctl(fid_l, VIDIOC_ENUMSTD, &stdvid) != -1) {

        printf("\tIndex:\t%d.\n",stdvid.index);
        printf("\tIdentification:\t%llu - It needs more details.\n", (unsigned long long)stdvid.id);
        printf("\tName:\t%s.\n",stdvid.name);
        printf("\tFrame period:\t%d/%d.\n",stdvid.frameperiod.numerator,stdvid.frameperiod.denominator);
        printf("\tLines per frame:\t%d.\n",stdvid.framelines);

        idx++;
        stdvid.index = idx;

    }

    if(!idx) printf("\tThere is no another video standards\n");

    return fid_l;
}

/*
 * Lista as entradas de áudio do dispositivo (VIDIOC_ENUMAUDIO).
 * Revisada: a estrutura agora é zerada a cada volta (os campos reservados precisam ser 0).
 */
int enumerate_audio_input(int fid_l) {

    int idx = 0;
    struct v4l2_audio Iaudio;

    printf("Enumerate Audio inputs:\n");

    CLEAR(Iaudio);
    Iaudio.index = idx;

    while (xioctl(fid_l, VIDIOC_ENUMAUDIO, &Iaudio) != -1) {

        printf("\tIndex:\t%d.\n", Iaudio.index);
        printf("\tName:\t%s.\n", Iaudio.name);

        idx++;
        CLEAR(Iaudio);                  /* limpa antes de pedir o próximo índice */
        Iaudio.index = idx;

    }

    if (!idx) {
        printf("\tThere are no audio inputs.\n");
    }

    return fid_l;

}

/*
 * Lista as saídas de áudio do dispositivo.
 * Revisada: usa VIDIOC_ENUMAUDOUT com struct v4l2_audioout. A versão anterior usava
 * VIDIOC_ENUMAUDIO (entradas), então mostrava as entradas como se fossem saídas.
 */
int enumerate_audio_output(int fid_l) {

    int idx = 0;
    struct v4l2_audioout Oaudio;

    printf("Enumerate Audio outputs:\n");

    CLEAR(Oaudio);
    Oaudio.index = idx;

    while (xioctl(fid_l, VIDIOC_ENUMAUDOUT, &Oaudio) != -1) {

        printf("\tIndex:\t%d.\n", Oaudio.index);
        printf("\tName:\t%s.\n", Oaudio.name);

        idx++;
        CLEAR(Oaudio);
        Oaudio.index = idx;

    }

    if (!idx) {
        printf("\tThere are no audio outputs.\n");
    }

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
    CLEAR(iput);
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
        CLEAR(iput);
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
    CLEAR(oput);
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
        CLEAR(oput);
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

/*
 * Mostra, em texto, a tabela de controles de UMA câmera (v4l2ctl_set): nome, tipo,
 * faixa real, valor atual em valor real e em percentual, e se aceita ajuste.
 * Substitui o uso da antiga tabela global camera_ctrl_table / num_controls_found:
 * agora a tabela é passada por quem a possui (uma por câmera).
 */
void list_controls_pct(const v4l2ctl_set *cs) {
    int i;
    char mn[64];

    if (!cs || cs->n == 0) {
        printf("Nenhum controle ajustavel.\n");
        return;
    }
    printf("%3s  %-28s %-8s %12s %12s %12s %7s  %s\n",
           "idx", "controle", "tipo", "min", "max", "atual", "%", "obs");
    for (i = 0; i < cs->n; i++) {
        const v4l2ctl_item *it = &cs->it[i];
        printf("%3d  %-28s %-8s %12lld %12lld %12lld %6.1f%%  %s%s%s",
               i, it->name, v4l2ctl_type_name(it->type),
               (long long)it->min, (long long)it->max, (long long)it->cur,
               v4l2ctl_raw_to_pct(it, it->cur),
               v4l2ctl_is_writable(it) ? "" : "[so leitura] ",
               (it->flags & V4L2_CTRL_FLAG_INACTIVE) ? "[inativo] " : "",
               (it->flags & V4L2_CTRL_FLAG_GRABBED)  ? "[preso] "   : "");
        /* Em menus mostra também o texto do item atual */
        if (0 == v4l2ctl_menu_name(cs, i, it->cur, mn, sizeof(mn)))
            printf("= %s", mn);
        printf("\n");
    }
    if (cs->dropped)
        printf("(%d controles nao couberam na tabela)\n", cs->dropped);
}

/*
 * Mostra os itens de um controle do tipo menu (VIDIOC_QUERYMENU).
 * Revisada: recebe o id e os limites como números. Antes, a enumeração dos controles
 * "legados" passava o id (um inteiro) no lugar do ponteiro esperado, e quebrava o
 * programa ao encontrar um menu.
 */
static void enumerate_menu_items(int fid_l, __u32 id, int min, int max) {

    struct v4l2_querymenu qmenu;
    int i;

    printf("\t  Menu items:\n");

    CLEAR(qmenu);
    qmenu.id = id;                              /* qual controle */

    for (i = min; i <= max; i++) {

        qmenu.index = i;                        /* qual item do menu */

        if (0 == xioctl(fid_l, VIDIOC_QUERYMENU, &qmenu)) {
            printf("\t    %s\n", qmenu.name);
        }                                       /* itens não suportados são pulados (EINVAL) */

    }

}

/* Mostra os itens de um controle do tipo menu obtido por VIDIOC_QUERY_EXT_CTRL. */
void enumerate_menu(struct v4l2_query_ext_ctrl *qXctrl, int fid_l){

    enumerate_menu_items(fid_l, qXctrl->id, (int)qXctrl->minimum, (int)qXctrl->maximum);

}

/*
 * Diz, em texto, POR QUE um controle não pode ser ajustado (ou "" se pode). A listagem
 * continua mostrando todos os controles do driver (é um diagnóstico), mas marca os que
 * não aceitam ajuste, em vez de deixar o leitor achar que Min = Max = 0 é um erro.
 * Critérios (os mesmos de v4l2ctl_is_writable/populate, em v4l2ctl.c):
 *   - tipo sem faixa em percentual (composto, texto, bitmask)   -> [nao ajustavel: tipo ...]
 *   - somente leitura                                           -> [unable: somente leitura]
 *   - Min == Max (nada para variar; exceto botão)               -> [unable: min = max]
 *   - Step = 0 em tipo inteiro/booleano/menu (driver incompleto; v4l2ctl trata como 1) -> [step = 0]
 *   - inativo / preso por outro controle                        -> [inativo] / [preso]
 * Devolve um ponteiro para buffer estático: use e descarte antes da próxima chamada.
 */
static const char* unable_reason(const struct v4l2_query_ext_ctrl *q) {
    static char buf[96];
    buf[0] = '\0';
    int numeric = (q->type == V4L2_CTRL_TYPE_INTEGER || q->type == V4L2_CTRL_TYPE_BOOLEAN ||
                   q->type == V4L2_CTRL_TYPE_MENU    || q->type == V4L2_CTRL_TYPE_INTEGER_MENU ||
                   q->type == V4L2_CTRL_TYPE_INTEGER64);

    if (!numeric && q->type != V4L2_CTRL_TYPE_BUTTON)
        snprintf(buf, sizeof buf, "   [nao ajustavel: tipo %u (composto/texto/bitmask)]", q->type);
    else if (q->flags & V4L2_CTRL_FLAG_READ_ONLY)
        snprintf(buf, sizeof buf, "   [unable: somente leitura]");
    else if (numeric && q->minimum == q->maximum)
        snprintf(buf, sizeof buf, "   [unable: min = max]");
    else if (numeric && q->step == 0)
        snprintf(buf, sizeof buf, "   [step = 0: tratado como 1]");
    else if (q->flags & V4L2_CTRL_FLAG_GRABBED)
        snprintf(buf, sizeof buf, "   [preso por outro controle]");
    else if (q->flags & V4L2_CTRL_FLAG_INACTIVE)
        snprintf(buf, sizeof buf, "   [inativo no momento]");
    return buf;
}

/*
 * Lista todos os controles do dispositivo (brilho, contraste, foco...), primeiro pela
 * interface estendida e depois pela interface antiga (controles privados).
 * Revisada: erro inesperado do driver agora devolve -1 em vez de encerrar o programa
 * (o multiview usa esta função e não pode morrer por causa de uma câmera).
 */
int enumerate_controls(int fid_l){

    struct v4l2_queryctrl       qctrl;
    struct v4l2_query_ext_ctrl  qXctrl;

    printf("Enumerate Controls:\n");

    /// ---- Interface estendida: percorre os controles com NEXT_CTRL ----
    CLEAR(qXctrl);
    qXctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;   /* "me dê o primeiro" */

    while (0 == xioctl(fid_l, VIDIOC_QUERY_EXT_CTRL, &qXctrl)) {

        if (!(qXctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {

            if (qXctrl.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
                /* Título de grupo ("User Controls"...): não é controle, vem com 0/0/0. Só separa a listagem. */
                printf("\t== %s ==\n", qXctrl.name);
            } else {
                printf("\t%s, Max = %lli, Min = %lli, Step = %llu%s\n",
                       qXctrl.name, (long long)qXctrl.maximum, (long long)qXctrl.minimum,
                       (unsigned long long)qXctrl.step, unable_reason(&qXctrl));
            }

            if (qXctrl.type == V4L2_CTRL_TYPE_MENU) {
                enumerate_menu(&qXctrl, fid_l);
            }
        }

        qXctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;   /* "agora o seguinte" */

    }

    if (errno != EINVAL) {              /* EINVAL é o fim normal da lista; outro erro é problema */
        perror("VIDIOC_QUERY_EXT_CTRL");
        return -1;
    }

    /// ---- Interface antiga: controles privados do fabricante ----
    CLEAR(qctrl);

    for (qctrl.id = V4L2_CID_PRIVATE_BASE; ; qctrl.id++) {

        if (0 == xioctl(fid_l, VIDIOC_QUERYCTRL, &qctrl)) {

            if (qctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
                continue;
            }

            printf("\t%s, Max = %d, Min = %d, Step = %d\n", qctrl.name, qctrl.maximum, qctrl.minimum, qctrl.step);

            if (qctrl.type == V4L2_CTRL_TYPE_MENU) {
                enumerate_menu_items(fid_l, qctrl.id, qctrl.minimum, qctrl.maximum);
            }

        } else {

            if (errno == EINVAL) {      /* acabaram os controles privados */
                break;
            }

            perror("VIDIOC_QUERYCTRL");
            return -1;

        }

    }

    return fid_l;

}

/* ========================================================================== */
/*  4. Escolha interativa (terminal)                                          */
/* ========================================================================== */

/*
 * Lê um inteiro da entrada padrão. Retorna 1 se obteve um valor em *v; 0 se a entrada
 * terminou (EOF). Texto que não é número é descartado e devolve *v = -1.
 * (O scanf("%d") puro repetia o mesmo erro para sempre quando o usuário digitava letras.)
 */
static int read_int(int *v) {

    int r = scanf("%d", v);
    int c;

    if (r == 1) {
        return 1;                       /* leu um número normalmente */
    }

    if (r == EOF) {
        return 0;                       /* entrada fechada: não há mais o que ler */
    }

    while ((c = getchar()) != '\n' && c != EOF) {
        /* descarta o resto da linha inválida */
    }

    *v = -1;                            /* valor inválido: o laço de quem chamou pergunta de novo */

    return (c == EOF) ? 0 : 1;

}

/*
 * Pede ao usuário um número entre minv e maxv (múltiplo de step) e repete até valer.
 * Se minv == maxv não há o que escolher. Se a entrada acabar (EOF) devolve minv.
 * Revisada: protege a divisão por step == 0 e usa read_int().
 */
int choose_value(int minv, int maxv, int step, char* tp) {

    int idx = -1;

    if(minv == maxv){
        return minv;                    /* só existe um valor possível */
    }

    if (step <= 0) {
        step = 1;                       /* evita divisão por zero em drivers que informam step = 0 */
    }

    while(idx < minv || idx > maxv || 0 != (idx % step)){

        idx = -1;

        printf("\tChoose a %s value and type ENTER: ", tp);

        if (!read_int(&idx)) {
            return minv;                /* sem entrada: usa o menor valor em vez de travar */
        }

    }

    return idx;

}

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
        while((unsigned int)tmp < fl.index){
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

/*
 * Pede no terminal o número do dispositivo desejado e grava o caminho em dv_nm.
 * Com um único dispositivo, escolhe-o sem perguntar. Se não houver escolha válida,
 * avisa e encerra (é o comportamento de linha de comando de choose_v4l2_device()).
 * Revisada: número negativo ou texto agora são recusados e a pergunta se repete;
 * entrada encerrada (EOF) cai no aviso "Device not exist".
 */
static void select_v4l2_device(char *dv_nm, v4l2_id *dv, int found_num) {

    int idx = INT_MAX;

    (void)dv;                           /* a lista é lida por get_v4l2_path(); o parâmetro ficou por compatibilidade */

    dv_nm[0] = 0;

    if(!found_num) return;

    if(found_num == 1){

        idx = 0;                        /* só há um: não precisa perguntar */

    }else if(found_num > 1) {

        while(idx >= found_num || idx < 0){

            printf("\n\tChoose device number and type ENTER: ");

            if (!read_int(&idx)) {      /* EOF: desiste */
                idx = -1;
                break;
            }

        }

        printf("\n\n");

    }

    get_v4l2_path(dv_nm,idx);           /* índice inválido gera caminho vazio */

    if(!dv_nm[0]) {

        printf("Device not exist\n");
        exit(EXIT_FAILURE);

    }

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

/*
 * Roda a sequência completa: lista os dispositivos V4L2, pergunta qual usar e deixa
 * o caminho escolhido em dev_name (lido depois por get_dev_name()).
 */
void choose_v4l2_device(){

    show_v4l2_list();

    select_v4l2_device(dev_name, v4l2_device_list, num_dev_found);

}
