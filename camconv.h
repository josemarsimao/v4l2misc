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
 *  camconv.h - CONVERSÃO DO FRAME BRUTO DA CÂMERA PARA IMAGEM RGB (OpenCV)
 *
 *  Etapa 5: estas conversões estavam dentro de multiview.cpp (o programa) mas são
 *  da biblioteca multicâmera: dependem só de viod e do OpenCV, não do multiview.
 *  ---------------------------------------------------------------------------
 */

#ifndef CAMCONV_H_INCLUDED
#define CAMCONV_H_INCLUDED

#include "camview.h"

/*
 * Converte o frame que acabou de chegar da câmera (vd.buffers[vd.bon]) para uma imagem
 * RGB de 3 canais em 'frame', usando o pixel format de vd.pxfmt e o tamanho vd.w x vd.h.
 * 'frame' precisa já estar alocada no tamanho certo (normalmente vd.v_mat.at(0)).
 * Cobre MJPEG/JPEG, YUYV, UYVY, YUV420, NV12/NV21, RGB24/BGR24, Bayer, Sonix, Y10/Y16...
 * Use dentro de cam_process_image() quando precisar da imagem colorida.
 */
void convertToRgb(viod &vd, Mat &frame);

#endif /* CAMCONV_H_INCLUDED */
