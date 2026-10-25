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
 *  camconv.cpp - CONVERSÃO DO FRAME BRUTO DA CÂMERA PARA IMAGEM RGB (implementação)
 *
 *  Veja camconv.h. Movido de multiview.cpp na etapa 5 sem alterar a lógica dos
 *  conversores (código de origem OpenCV/libv4lconvert, com os comentários originais).
 *  ---------------------------------------------------------------------------
 */

#include "camconv.h"

/// source: OPenCv Code
/// https://github.com/opencv/opencv/blob/master/modules/videoio/src/cap_v4l.cpp
/// ****************************************************************************
/// libv4lconvert has a more detailed version


/*
 * Turn a YUV4:2:0 block into an RGB block
 *
 * Video4Linux seems to use the blue, green, red channel
 * order convention-- rgb[0] is blue, rgb[1] is green, rgb[2] is red.
 *
 * Color space conversion coefficients taken from the excellent
 * http://www.inforamp.net/~poynton/ColorFAQ.html
 * In his terminology, this is a CCIR 601.1 YCbCr -> RGB.
 * Y values are given for all 4 pixels, but the U (Pb)
 * and V (Pr) are assumed constant over the 2x2 block.
 *
 * To avoid floating point arithmetic, the color conversion
 * coefficients are scaled into 16.16 fixed-point integers.
 * They were determined as follows:
 *
 *  double brightness = 1.0;  (0->black; 1->full scale)
 *  double saturation = 1.0;  (0->greyscale; 1->full color)
 *  double fixScale = brightness * 256 * 256;
 *  int rvScale = (int)(1.402 * saturation * fixScale);
 *  int guScale = (int)(-0.344136 * saturation * fixScale);
 *  int gvScale = (int)(-0.714136 * saturation * fixScale);
 *  int buScale = (int)(1.772 * saturation * fixScale);
 *  int yScale = (int)(fixScale);
 */

/* LIMIT: convert a 16.16 fixed-point value to a byte, with clipping. */
#define LIMIT(x) ((x)>0xffffff?0xff: ((x)<=0xffff?0:((x)>>16)))

static inline void move_411_block(int yTL, int yTR, int yBL, int yBR, int u, int v, int /*rowPixels*/, unsigned char * rgb){

    const int rvScale = 91881;
    const int guScale = -22553;
    const int gvScale = -46801;
    const int buScale = 116129;
    const int yScale  = 65536;
    int r, g, b;

    g = guScale * u + gvScale * v;
    //  if (force_rgb) {
    //      r = buScale * u;
    //      b = rvScale * v;
    //  } else {
    r = rvScale * v;
    b = buScale * u;
    //  }

    yTL *= yScale; yTR *= yScale;
    yBL *= yScale; yBR *= yScale;

    /* Write out top two first pixels */
    rgb[0] = LIMIT(b+yTL); rgb[1] = LIMIT(g+yTL);
    rgb[2] = LIMIT(r+yTL);

    rgb[3] = LIMIT(b+yTR); rgb[4] = LIMIT(g+yTR);
    rgb[5] = LIMIT(r+yTR);

    /* Write out top two last pixels */
    rgb += 6;
    rgb[0] = LIMIT(b+yBL); rgb[1] = LIMIT(g+yBL);
    rgb[2] = LIMIT(r+yBL);

    rgb[3] = LIMIT(b+yBR); rgb[4] = LIMIT(g+yBR);
    rgb[5] = LIMIT(r+yBR);
}

static inline void move_m420_block(int yTL, int yTR, int yBL, int yBR, int u, int v, int width, unsigned char * rgb){

    const int rvScale = 91881;
    const int guScale = -22553;
    const int gvScale = -46801;
    const int buScale = 116129;
    const int yScale  = 65536;
    int r, g, b;

    g = guScale * u + gvScale * v;
    //  if (force_rgb) {
    //      r = buScale * u;
    //      b = rvScale * v;
    //  } else {
    r = rvScale * v;
    b = buScale * u;
    //  }

    yTL *= yScale; yTR *= yScale;
    yBL *= yScale; yBR *= yScale;

    /* Write out top two first pixels */
    rgb[0] = LIMIT(b+yTL); rgb[1] = LIMIT(g+yTL);
    rgb[2] = LIMIT(r+yTL);

    rgb[3] = LIMIT(b+yTR); rgb[4] = LIMIT(g+yTR);
    rgb[5] = LIMIT(r+yTR);

    /* Write out top two last pixels */
    rgb += (3*width);
    rgb[0] = LIMIT(b+yBL); rgb[1] = LIMIT(g+yBL);
    rgb[2] = LIMIT(r+yBL);

    rgb[3] = LIMIT(b+yBR); rgb[4] = LIMIT(g+yBR);
    rgb[5] = LIMIT(r+yBR);
}

// Consider a YUV411P image of 8x2 pixels.
//
// A plane of Y values as before.
//
// A plane of U values    1       2
//                        3       4
//
// A plane of V values    1       2
//                        3       4
//
// The U1/V1 samples correspond to the ABCD pixels.
//     U2/V2 samples correspond to the EFGH pixels.
//
/* Converts from planar YUV411P to RGB24. */
/* [FD] untested... */
static void yuv411p_to_rgb24(int width, int height, unsigned char *pIn0, unsigned char *pOut0){

    const int numpix = width * height;
    const int bytes = 24 >> 3;
    int i, j, y00, y01, y10, y11, u, v;
    unsigned char *pY = pIn0;
    unsigned char *pU = pY + numpix;
    unsigned char *pV = pU + numpix / 4;
    unsigned char *pOut = pOut0;

    for (j = 0; j <= height; j++) {
        for (i = 0; i <= width - 4; i += 4) {
            y00 = *pY;
            y01 = *(pY + 1);
            y10 = *(pY + 2);
            y11 = *(pY + 3);
            u = (*pU++) - 128;
            v = (*pV++) - 128;

            move_411_block(y00, y01, y10, y11, u, v, width, pOut);

            pY += 4;
            pOut += 4 * bytes;

        }
    }
}

/// V4L2_PIX_FMT_M420 to V4L2_PIX_FMT_BGR24
static void m420_to_rgb24(int width, int height, unsigned char *pIn0, unsigned char *pOut0){

    const int dbW = width * 2;
    const int bytes = 24 >> 3;
    int i, j, y00, y01, y10, y11, u, v;

    unsigned char *pOut = pOut0;

    unsigned char *pY = pIn0;



    for (j = 0; j <= height - 2; j += 2) {
        for (i = 0; i <= width - 2; i += 2) {
            y00 = *pY;
            y01 = *(pY + 1);
            y10 = *(pY + width);
            y11 = *(pY + width + 1);
            u = *(pY+dbW) - 128;
            v = *(pY+dbW+1) - 128;

            move_m420_block(y00, y01, y10, y11, u, v, width, pOut);

            pY += 2;
            pOut += 2 * bytes;

        }
        pY += dbW;
        pOut += (3 * width);
    }

    pOut = pOut;
}

/*
 * BAYER2RGB24 ROUTINE TAKEN FROM:
 *
 * Sonix SN9C10x based webcam basic I/F routines
 * Takafumi Mizuno <taka-qce@ls-a.jp>
 *
 */
static void bayer2rgb24(long int WIDTH, long int HEIGHT, unsigned char *src, unsigned char *dst){

    long int i;
    unsigned char *rawpt, *scanpt;
    long int size;

    rawpt = src;
    scanpt = dst;
    size = WIDTH*HEIGHT;

    for ( i = 0; i < size; i++ ) {
        if ( (i/WIDTH) % 2 == 0 ) {
            if ( (i % 2) == 0 ) {
                /* B */
                if ( (i > WIDTH) && ((i % WIDTH) > 0) ) {
                    *scanpt++ = (*(rawpt-WIDTH-1)+*(rawpt-WIDTH+1)+
                            *(rawpt+WIDTH-1)+*(rawpt+WIDTH+1))/4;  /* R */
                    *scanpt++ = (*(rawpt-1)+*(rawpt+1)+
                            *(rawpt+WIDTH)+*(rawpt-WIDTH))/4;      /* G */
                    *scanpt++ = *rawpt;                                     /* B */
                } else {
                    /* first line or left column */
                    *scanpt++ = *(rawpt+WIDTH+1);           /* R */
                    *scanpt++ = (*(rawpt+1)+*(rawpt+WIDTH))/2;      /* G */
                    *scanpt++ = *rawpt;                             /* B */
                }
            } else {
                /* (B)G */
                if ( (i > WIDTH) && ((i % WIDTH) < (WIDTH-1)) ) {
                    *scanpt++ = (*(rawpt+WIDTH)+*(rawpt-WIDTH))/2;  /* R */
                    *scanpt++ = *rawpt;                                     /* G */
                    *scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;          /* B */
                } else {
                    /* first line or right column */
                    *scanpt++ = *(rawpt+WIDTH);     /* R */
                    *scanpt++ = *rawpt;             /* G */
                    *scanpt++ = *(rawpt-1); /* B */
                }
            }
        } else {
            if ( (i % 2) == 0 ) {
                /* G(R) */
                if ( (i < (WIDTH*(HEIGHT-1))) && ((i % WIDTH) > 0) ) {
                    *scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;          /* R */
                    *scanpt++ = *rawpt;                                     /* G */
                    *scanpt++ = (*(rawpt+WIDTH)+*(rawpt-WIDTH))/2;  /* B */
                } else {
                    /* bottom line or left column */
                    *scanpt++ = *(rawpt+1);         /* R */
                    *scanpt++ = *rawpt;                     /* G */
                    *scanpt++ = *(rawpt-WIDTH);             /* B */
                }
            } else {
                /* R */
                if ( i < (WIDTH*(HEIGHT-1)) && ((i % WIDTH) < (WIDTH-1)) ) {
                    *scanpt++ = *rawpt;                                     /* R */
                    *scanpt++ = (*(rawpt-1)+*(rawpt+1)+
                            *(rawpt-WIDTH)+*(rawpt+WIDTH))/4;      /* G */
                    *scanpt++ = (*(rawpt-WIDTH-1)+*(rawpt-WIDTH+1)+
                            *(rawpt+WIDTH-1)+*(rawpt+WIDTH+1))/4;  /* B */
                } else {
                    /* bottom line or right column */
                    *scanpt++ = *rawpt;                             /* R */
                    *scanpt++ = (*(rawpt-1)+*(rawpt-WIDTH))/2;      /* G */
                    *scanpt++ = *(rawpt-WIDTH-1);           /* B */
                }
            }
        }
        rawpt++;
    }

}


/*
 * HISTÓRICO E ANÁLISE DE IMPLEMENTAÇÃO: sgbrg2rgb24
 *
 * CONTEXTO HISTÓRICO:
 * Esta função é uma relíquia do desenvolvimento inicial do ecossistema de vídeo
 * no Linux e OpenCV (período entre 2003 e 2008). Ela foi introduzida originalmente
 * no arquivo 'cap_v4l.cpp' (OpenCV highgui module) para adicionar suporte a webcams
 * baseadas no chip processador Sonix SN9C10x (como a Logitech QuickCam Express Plus).
 *
 * CRÉDITOS E REFERÊNCIAS:
 * - Código de Decodificação Original: Desenvolvido por Takafumi Mizuno <taka-qce@ls-a.jp>.
 * - Integração no OpenCV (Patch 5): Realizada em 7 de Setembro de 2005 por Csaba Kertesz <sign@freemail.hu>.
 * - Referência do Código-Fonte: https://github.com
 *
 * A "PARADOXO" DOS COMENTÁRIOS INVERTIDOS (RGB vs BGR):
 * Ao analisar o código, há uma aparente contradição: os comentários internos indicam
 * a gravação na ordem R, G, B, mas a matemática de indexação do sensor Bayer parece
 * ler o padrão GRBG em vez de GBRG. Isso acontece por que o padrão do OpenCV é BGR.
 *
 * CONCLUSÃO:
 * O nome da função 'sgbrg2rgb24' está CORRETO quanto ao formato do sensor de entrada (GBRG: Verde, Azul).
 * No entanto, os comentários internos de atribuição de cor  estão INVERTIDOS.
 */


// SGBRG to RGB24
/// fonte:   https://github.com/egonSchiele/OpenCV/blob/master/modules/highgui/src/cap_v4l.cpp
// for some reason, red and blue needs to be swapped
// at least for  046d:092f Logitech, Inc. QuickCam Express Plus to work
//see: http://www.siliconimaging.com/RGB%20Bayer.htm
//and 4.6 at http://tldp.org/HOWTO/html_single/libdc1394-HOWTO/
static void sgbrg2rgb24(long int WIDTH, long int HEIGHT, unsigned char *src, unsigned char *dst){

    long int i;
    unsigned char *rawpt, *scanpt;
    long int size;

    rawpt = src;
    scanpt = dst;
    size = WIDTH*HEIGHT;

    for ( i = 0; i < size; i++ )
    {
        if ( (i/WIDTH) % 2 == 0 ) //even row
        {
            if ( (i % 2) == 0 ) //even pixel
            {
                if ( (i > WIDTH) && ((i % WIDTH) > 0) )
                {
                    *scanpt++ = (*(rawpt-1)+*(rawpt+1))/2;       /* R */
                    *scanpt++ = *(rawpt);                        /* G */
                    *scanpt++ = (*(rawpt-WIDTH) + *(rawpt+WIDTH))/2;      /* B */
                } else
                {
                    /* first line or left column */

                    *scanpt++ = *(rawpt+1);           /* R */
                    *scanpt++ = *(rawpt);             /* G */
                    *scanpt++ =  *(rawpt+WIDTH);      /* B */
                }
            } else //odd pixel
            {
                if ( (i > WIDTH) && ((i % WIDTH) < (WIDTH-1)) )
                {
                    *scanpt++ = *(rawpt);       /* R */
                    *scanpt++ = (*(rawpt-1)+*(rawpt+1)+*(rawpt-WIDTH)+*(rawpt+WIDTH))/4; /* G */
                    *scanpt++ = (*(rawpt-WIDTH-1) + *(rawpt-WIDTH+1) + *(rawpt+WIDTH-1) + *(rawpt+WIDTH+1))/4;      /* B */
                } else
                {
                    /* first line or right column */

                    *scanpt++ = *(rawpt);       /* R */
                    *scanpt++ = (*(rawpt-1)+*(rawpt+WIDTH))/2; /* G */
                    *scanpt++ = *(rawpt+WIDTH-1);      /* B */
                }
            }
        } else
        { //odd row
            if ( (i % 2) == 0 ) //even pixel
            {
                if ( (i < (WIDTH*(HEIGHT-1))) && ((i % WIDTH) > 0) )
                {
                    *scanpt++ =  (*(rawpt-WIDTH-1)+*(rawpt-WIDTH+1)+*(rawpt+WIDTH-1)+*(rawpt+WIDTH+1))/4;          /* R */
                    *scanpt++ =  (*(rawpt-1)+*(rawpt+1)+*(rawpt-WIDTH)+*(rawpt+WIDTH))/4;      /* G */
                    *scanpt++ =  *(rawpt); /* B */
                } else
                {
                    /* bottom line or left column */

                    *scanpt++ =  *(rawpt-WIDTH+1);          /* R */
                    *scanpt++ =  (*(rawpt+1)+*(rawpt-WIDTH))/2;      /* G */
                    *scanpt++ =  *(rawpt); /* B */
                }
            } else
            { //odd pixel
                if ( i < (WIDTH*(HEIGHT-1)) && ((i % WIDTH) < (WIDTH-1)) )
                {
                    *scanpt++ = (*(rawpt-WIDTH)+*(rawpt+WIDTH))/2;  /* R */
                    *scanpt++ = *(rawpt);      /* G */
                    *scanpt++ = (*(rawpt-1)+*(rawpt+1))/2; /* B */
                } else
                {
                    /* bottom line or right column */

                    *scanpt++ = (*(rawpt-WIDTH));  /* R */
                    *scanpt++ = *(rawpt);      /* G */
                    *scanpt++ = (*(rawpt-1)); /* B */
                }
            }
        }
        rawpt++;
    }
}




#define CLAMP(x)        ((x)<0?0:((x)>255)?255:(x))

typedef struct {
    int is_abs;
    int len;
    int val;
} code_table_t;


/* local storage */
static code_table_t table[256];
static int init_done = 0;

/*
  sonix_decompress_init
  =====================
    pre-calculates a locally stored table for efficient huffman-decoding.
  Each entry at index x in the table represents the codeword
  present at the MSB of byte x.
 */
static void sonix_decompress_init(void){

    int i;
    int is_abs, val, len;

    for (i = 0; i < 256; i++) {
        is_abs = 0;
        val = 0;
        len = 0;
        if ((i & 0x80) == 0) {
            /* code 0 */
            val = 0;
            len = 1;
        }
        else if ((i & 0xE0) == 0x80) {
            /* code 100 */
            val = +4;
            len = 3;
        }
        else if ((i & 0xE0) == 0xA0) {
            /* code 101 */
            val = -4;
            len = 3;
        }
        else if ((i & 0xF0) == 0xD0) {
            /* code 1101 */
            val = +11;
            len = 4;
        }
        else if ((i & 0xF0) == 0xF0) {
            /* code 1111 */
            val = -11;
            len = 4;
        }
        else if ((i & 0xF8) == 0xC8) {
            /* code 11001 */
            val = +20;
            len = 5;
        }
        else if ((i & 0xFC) == 0xC0) {
            /* code 110000 */
            val = -20;
            len = 6;
        }
        else if ((i & 0xFC) == 0xC4) {
            /* code 110001xx: unknown */
            val = 0;
            len = 8;
        }
        else if ((i & 0xF0) == 0xE0) {
            /* code 1110xxxx */
            is_abs = 1;
            val = (i & 0x0F) << 4;
            len = 8;
        }
        table[i].is_abs = is_abs;
        table[i].val = val;
        table[i].len = len;
    }

    init_done = 1;
}

/*
  sonix_decompress
  ================
    decompresses an image encoded by a SN9C101 camera controller chip.
  IN    width
    height
    inp         pointer to compressed frame (with header already stripped)
  OUT   outp    pointer to decompressed frame
  Returns 0 if the operation was successful.
  Returns <0 if operation failed.
 */
static int sonix_decompress(int width, int height, unsigned char *inp, unsigned char *outp)
{
    int row, col;
    int val;
    int bitpos;
    unsigned char code;
    unsigned char *addr;

    if (!init_done) {
        /* do sonix_decompress_init first! */
        ///return -1;
        sonix_decompress_init();
    }

    bitpos = 0;
    for (row = 0; row < height; row++) {

        col = 0;



        /* first two pixels in first two rows are stored as raw 8-bit */
        if (row < 2) {
            addr = inp + (bitpos >> 3);
            code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));
            bitpos += 8;
            *outp++ = code;

            addr = inp + (bitpos >> 3);
            code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));
            bitpos += 8;
            *outp++ = code;

            col += 2;
        }

        while (col < width) {
            /* get bitcode from bitstream */
            addr = inp + (bitpos >> 3);
            code = (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));

            /* update bit position */
            bitpos += table[code].len;

            /* calculate pixel value */
            val = table[code].val;
            if (!table[code].is_abs) {
                /* value is relative to top and left pixel */
                if (col < 2) {
                    /* left column: relative to top pixel */
                    val += outp[-2*width];
                }
                else if (row < 2) {
                    /* top row: relative to left pixel */
                    val += outp[-2];
                }
                else {
                    /* main area: average of left pixel and top pixel */
                    val += (outp[-2] + outp[-2*width]) / 2;
                }
            }

            /* store pixel */
            *outp++ = CLAMP(val);
            col++;
        }
    }

    return 0;
}


/*
 * Converte o frame atual da câmera (vd.buffers[vd.bon]) para RGB em 'frame'.
 *
 * Funcionamento: primeiro tenta os formatos que têm conversor próprio neste arquivo
 * (YUV411P, Bayer, Sonix, M420); se o formato não for um deles, usa o OpenCV
 * (cvtColor / imdecode). Formatos desconhecidos são copiados bruto como se já fossem BGR.
 * Chamada pela cam_process_image() padrão; veja camview.cpp.
 */
// Bayer 8 bits de qualquer fase -> quadro BGR do OpenCV (bytes B,G,R), interpolacao bilinear.
// 'fase' lista as cores dos 4 pontos do mosaico na ordem (0,0) (0,1) (1,0) (1,1); 0 = R, 1 = G, 2 = B.
// Para cada pixel e cada cor faz a media das amostras daquela cor na vizinhanca 3x3 (so as que estao dentro da imagem):
// no proprio pixel da cor vale ele mesmo; no verde de um R/B vale a cruz; no R de um B vale as 4 diagonais.
static void bayer_phase2rgb24(long int WIDTH, long int HEIGHT, const unsigned char *src, unsigned char *dst, const unsigned char fase[4]){
    for (long int y = 0; y < HEIGHT; y++){
        for (long int x = 0; x < WIDTH; x++){
            int sum[3] = {0, 0, 0}, cnt[3] = {0, 0, 0};
            for (long int dy = -1; dy <= 1; dy++){
                long int yy = y + dy;
                if (yy < 0 || yy >= HEIGHT) continue;
                for (long int dx = -1; dx <= 1; dx++){
                    long int xx = x + dx;
                    if (xx < 0 || xx >= WIDTH) continue;
                    int c = fase[((yy & 1) << 1) | (xx & 1)];
                    sum[c] += src[yy * WIDTH + xx];
                    cnt[c]++;
                }
            }
            for (int c = 2; c >= 0; c--)    // ordem B, G, R: o quadro do OpenCV e' BGR
                *dst++ = cnt[c] ? (unsigned char)(sum[c] / cnt[c]) : 0;
        }
    }
}

static const unsigned char FASE_BGGR[4] = {2, 1, 1, 0};   // B G / G R
static const unsigned char FASE_GBRG[4] = {1, 2, 0, 1};   // G B / R G
static const unsigned char FASE_GRBG[4] = {1, 0, 2, 1};   // G R / B G
static const unsigned char FASE_RGGB[4] = {0, 1, 1, 2};   // R G / G B

void convertToRgb(viod &vd, Mat &frame){

    // Pega o buffer que o núcleo acabou de preencher (vd.bon é o índice dele).

    /// Verificar uma forma melhor depois
    vbuff currentBuffer;
    currentBuffer.start = vd.buffers[vd.bon].start;
    currentBuffer.length = vd.buffers[vd.bon].length;
    currentBuffer.binf = vd.buffers[vd.bon].binf;
    /// ******************************************

    cv::Size imageSize(vd.w, vd.h);     // tamanho do frame em pixels
    // 1ª etapa: formatos com conversor próprio (escrevem direto em frame.data)
    switch (vd.pxfmt){

        case V4L2_PIX_FMT_YUV411P:
            yuv411p_to_rgb24(imageSize.width, imageSize.height, (unsigned char*)(currentBuffer.start), (unsigned char*)frame.data);
            return;

///        case V4L2_PIX_FMT_SBGGR8:
///            bayer2rgb24(imageSize.width, imageSize.height, (unsigned char*)currentBuffer.start, (unsigned char*)frame.data);
///            return;

///        case V4L2_PIX_FMT_SGBRG8:
///            sgbrg2rgb24(imageSize.width, imageSize.height, (unsigned char*)currentBuffer.start, (unsigned char*)frame.data);
///            return;

        // Os 4 formatos Bayer brutos, cada um com a fase do seu mosaico (ver FASE_*)
        case V4L2_PIX_FMT_SBGGR8:       // B G / G R   (BA81)
            bayer_phase2rgb24(imageSize.width, imageSize.height, (unsigned char*)currentBuffer.start, (unsigned char*)frame.data, FASE_BGGR);
            return;

        case V4L2_PIX_FMT_SGBRG8:       // G B / R G
            bayer_phase2rgb24(imageSize.width, imageSize.height, (unsigned char*)currentBuffer.start, (unsigned char*)frame.data, FASE_GBRG);
            return;

        case V4L2_PIX_FMT_SGRBG8:       // G R / B G
            bayer_phase2rgb24(imageSize.width, imageSize.height, (unsigned char*)currentBuffer.start, (unsigned char*)frame.data, FASE_GRBG);
            return;

        case V4L2_PIX_FMT_SRGGB8:       // R G / G B
            bayer_phase2rgb24(imageSize.width, imageSize.height, (unsigned char*)currentBuffer.start, (unsigned char*)frame.data, FASE_RGGB);
            return;

        case V4L2_PIX_FMT_SN9C10X:
            ///sonix_decompress_init();
            sonix_decompress(imageSize.width, imageSize.height, (unsigned char*)currentBuffer.start, vd.xbuf);
            bayer2rgb24(imageSize.width, imageSize.height, vd.xbuf, (unsigned char*)frame.data);

            return;

        case V4L2_PIX_FMT_M420:
            ///cv::cvtColor(cv::Mat(imageSize.height * 3 / 2, imageSize.width, CV_8U, currentBuffer.start), destination, COLOR_YUV2BGR_IYUV);
            m420_to_rgb24(imageSize.width, imageSize.height, (unsigned char*)(currentBuffer.start), (unsigned char*)frame.data);
            return;
        default:
            break;

    }
    // 2ª etapa: formatos convertidos pelo OpenCV. 'destination' é só uma visão de frame.data
    // (mesma memória), então o resultado já fica em 'frame'.
    cv::Mat destination(imageSize, CV_8UC3, frame.data);
    switch (vd.pxfmt){

        case V4L2_PIX_FMT_YVU420:
            cv::cvtColor(cv::Mat(imageSize.height * 3 / 2, imageSize.width, CV_8U, currentBuffer.start), destination, COLOR_YUV2BGR_YV12);
            return;

        case V4L2_PIX_FMT_YUV420:
            cv::cvtColor(cv::Mat(imageSize.height * 3 / 2, imageSize.width, CV_8U, currentBuffer.start), destination, COLOR_YUV2BGR_IYUV);
            return;

        case V4L2_PIX_FMT_NV12:
            cv::cvtColor(cv::Mat(imageSize.height * 3 / 2, imageSize.width, CV_8U, currentBuffer.start), destination, COLOR_YUV2RGB_NV12);
            return;

        case V4L2_PIX_FMT_NV21:
            cv::cvtColor(cv::Mat(imageSize.height * 3 / 2, imageSize.width, CV_8U, currentBuffer.start), destination, COLOR_YUV2RGB_NV21);
            return;

///    #ifdef HAVE_JPEG
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
        {
            /// Must include IMREAD_IGNORE_ORIENTATION flag
            imdecode(Mat(1, currentBuffer.binf.bytesused, CV_8UC1, currentBuffer.start), IMREAD_COLOR+IMREAD_IGNORE_ORIENTATION, &destination);
            return;
        }
///    #endif

        case V4L2_PIX_FMT_YUYV:
            cvtColor(cv::Mat(imageSize, CV_8UC2, currentBuffer.start), destination, COLOR_YUV2BGR_YUYV);
            return;

        case V4L2_PIX_FMT_UYVY:
            cvtColor(cv::Mat(imageSize, CV_8UC2, currentBuffer.start), destination, COLOR_YUV2BGR_UYVY);
            return;

        case V4L2_PIX_FMT_RGB24:
            cvtColor(cv::Mat(imageSize, CV_8UC3, currentBuffer.start), destination, COLOR_RGB2BGR);
            return;

        case V4L2_PIX_FMT_Y16:
        {
            Mat temp(imageSize, CV_8UC1, vd.xbuf);
            Mat(imageSize, CV_16UC1, currentBuffer.start).convertTo(temp, CV_8U, 1.0 / 256);
            cvtColor(temp, destination, COLOR_GRAY2BGR);
            return;
        }
        case V4L2_PIX_FMT_Y10:
        {
            cv::Mat temp(imageSize, CV_8UC1, vd.xbuf);
            cv::Mat(imageSize, CV_16UC1, currentBuffer.start).convertTo(temp, CV_8U, 1.0 / 4);
            cv::cvtColor(temp, destination, COLOR_GRAY2BGR);
            return;
        }
        case V4L2_PIX_FMT_GREY:
            cv::cvtColor(cv::Mat(imageSize, CV_8UC1, currentBuffer.start), destination, COLOR_GRAY2BGR);
            break;
        case V4L2_PIX_FMT_H264:
        {
            ///Mat tp(1, currentBuffer.binf.bytesused, CV_8U, currentBuffer.start);
            Mat tp(imageSize.height * 3 / 2, imageSize.width, CV_8U, currentBuffer.start);

            cv::cvtColor(tp, destination, CV_YUV2BGR_I420);
            break;
        }
        case V4L2_PIX_FMT_BGR24:
        default:
            memcpy((char *)frame.data, (char *)currentBuffer.start, currentBuffer.binf.bytesused);
            break;

    }
}

